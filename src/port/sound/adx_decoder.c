#include "port/sound/adx_decoder.h"

#include <SDL3/SDL.h>

bool ADXDecoder_Init(ADXDecoder* decoder, SDL_IOStream* file) {
    ADXHeader* header = &decoder->header;

    if (file == NULL) {
        return false;
    }

    SDL_zerop(header);

    Uint16 copyright_offset;
    SDL_SeekIO(file, 2, SDL_IO_SEEK_SET);
    SDL_ReadU16BE(file, &copyright_offset);
    header->copyright_offset = copyright_offset;
    header->data_offset = header->copyright_offset + 4;

    char copyright_str[sizeof(CRI_COPYRIGHT_STR) - 1];
    SDL_SeekIO(file, header->copyright_offset - 2, SDL_IO_SEEK_SET);
    SDL_ReadIO(file, copyright_str, sizeof(copyright_str));

    if (SDL_strncmp(copyright_str, CRI_COPYRIGHT_STR, sizeof(copyright_str)) != 0) {
        return false;
    }

    SDL_SeekIO(file, 4, SDL_IO_SEEK_SET);
    SDL_ReadU8(file, &header->encoding);

    if (header->encoding != ADX_ENCODING_STANDARD) {
        return false;
    }

    SDL_ReadU8(file, &header->block_size);

    if (header->block_size != 18) {
        return false;
    }

    SDL_ReadU8(file, &header->sample_bitdepth);

    if (header->sample_bitdepth != 4) {
        return false;
    }

    SDL_ReadU8(file, &header->channel_count);

    if (header->channel_count == 0 || header->channel_count > ADX_CHANNELS_MAX) {
        return false;
    }

    SDL_ReadU32BE(file, &header->sample_rate);
    SDL_ReadU32BE(file, &header->total_samples);
    SDL_ReadU16BE(file, &header->highpass_frequency);
    SDL_ReadU8(file, &header->version);
    SDL_ReadU8(file, &header->flags);

    Uint32 loop_enabled = 0;

    switch (header->version) {
    case 3:
        SDL_SeekIO(file, 0x14, SDL_IO_SEEK_SET);
        SDL_ReadU16BE(file, &header->loop_alignment_samples);

        Uint16 loop_enabled_16;
        SDL_SeekIO(file, 0x16, SDL_IO_SEEK_SET);
        SDL_ReadU16BE(file, &loop_enabled_16);
        loop_enabled = loop_enabled_16;

        if (loop_enabled == 1) {
            SDL_SeekIO(file, 0x1C, SDL_IO_SEEK_SET);
            SDL_ReadU32BE(file, &header->loop_begin_sample);
            SDL_ReadU32BE(file, &header->loop_begin_byte);
            SDL_ReadU32BE(file, &header->loop_end_sample);
            SDL_ReadU32BE(file, &header->loop_end_byte);
        }

        break;

    case 4:
        SDL_SeekIO(file, 0x24, SDL_IO_SEEK_SET);
        SDL_ReadU32BE(file, &loop_enabled);

        if (loop_enabled == 1) {
            SDL_ReadU32BE(file, &header->loop_begin_sample);
            SDL_ReadU32BE(file, &header->loop_begin_byte);
            SDL_ReadU32BE(file, &header->loop_end_sample);
            SDL_ReadU32BE(file, &header->loop_end_byte);
        }

        break;

    default:
        return false;
    }

    header->loop_enabled = (loop_enabled == 1);

    const double a =
        SDL_sqrt(2.0) - SDL_cos(2.0 * SDL_PI_D * ((double)header->highpass_frequency / header->sample_rate));
    const double b = SDL_sqrt(2.0) - 1.0;
    const double c = (a - SDL_sqrt((a + b) * (a - b))) / b;
    decoder->coefficients[0] = (Sint32)SDL_floor(c * 2.0 * 8192.0);
    decoder->coefficients[1] = (Sint32)SDL_floor(-(c * c) * 8192.0);

    decoder->sample_index = 0;
    SDL_zeroa(decoder->past_samples);
    decoder->file = file;
    return true;
}

Uint32 ADXDecoder_Decode(ADXDecoder* decoder, Sint16* buffer, Uint32 samples_needed) {
    const int channel_count = decoder->header.channel_count;
    const Sint32 coefficient_0 = decoder->coefficients[0];
    const Sint32 coefficient_1 = decoder->coefficients[1];
    Uint8 frame[ADX_BLOCK_SIZE * ADX_CHANNELS_MAX];
    Uint32 samples_read = 0;

    while ((samples_read < samples_needed) && (decoder->sample_index < decoder->header.total_samples)) {
        Uint32 sample_offset = decoder->sample_index % ADX_SAMPLES_PER_BLOCK;
        Uint32 samples_to_read = ADX_SAMPLES_PER_BLOCK - sample_offset;
        samples_to_read = SDL_min(samples_to_read, samples_needed - samples_read);
        samples_to_read = SDL_min(samples_to_read, decoder->header.total_samples - decoder->sample_index);

        const Uint32 frame_start =
            decoder->header.data_offset +
            decoder->sample_index / ADX_SAMPLES_PER_BLOCK * ADX_BLOCK_SIZE * channel_count;
        const size_t frame_size = ADX_BLOCK_SIZE * channel_count;

        // One read per block. Reading every byte through the stream made decoding cost about
        // 9% of a frame on the Vita, and 35 ms when a track starts
        SDL_zeroa(frame);

        if (SDL_SeekIO(decoder->file, frame_start, SDL_IO_SEEK_SET) >= 0) {
            SDL_ReadIO(decoder->file, frame, frame_size);
        }

        for (int i = 0; i < channel_count; i++) {
            const Uint8* block = &frame[ADX_BLOCK_SIZE * i];
            const Sint32 scale = (block[0] << 8) | block[1];
            Sint32 past_0 = decoder->past_samples[i][0];
            Sint32 past_1 = decoder->past_samples[i][1];
            Sint16* out = buffer + i;

            for (Uint32 offset = sample_offset; offset < sample_offset + samples_to_read; offset++) {
                const Sint32 sample_prediction = (coefficient_0 * past_0 + coefficient_1 * past_1) >> 13;
                const Uint8 byte = block[2 + offset / (8 / ADX_SAMPLE_BITDEPTH)];
                Sint32 sample_error = (offset & 1) ? (byte & 0xF) : (byte >> 4);

                if (sample_error & 0b1000) { // Sign extension
                    sample_error |= ~0b1111;
                }

                const Sint32 sample = sample_error * scale + sample_prediction;
                past_1 = past_0;
                past_0 = sample;

                *out = (Sint16)SDL_clamp(sample, SDL_MIN_SINT16, SDL_MAX_SINT16);
                out += channel_count;
            }

            decoder->past_samples[i][0] = past_0;
            decoder->past_samples[i][1] = past_1;
        }

        buffer += samples_to_read * channel_count;
        decoder->sample_index += samples_to_read;
        samples_read += samples_to_read;
    }

    return samples_read;
}
