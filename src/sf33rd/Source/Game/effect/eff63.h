#ifndef EFF63_H
#define EFF63_H

#include "structs.h"
#include "types.h"

void effect_63_move(WORK_Other_CONN* ewk);
s32 effect_63_init(u8 dir_old, s16 sync_bg, s16 master_player, s16 letter_type, s16 cursor_index);

/// The same values in the small font, for the Vita's Screen Adjust page.
/// `letter_type` 4 shows the picture shape, 5 the filter and 6 the filter intensity
s32 effect_63_init_compact(u8 dir_old, s16 sync_bg, s16 master_player, s16 letter_type, s16 cursor_index);

/// The entries of `Convert_Buff[2][0]` that hold the picture shape, the filter and the filter intensity while the
/// Screen Adjust page is open
#define SCREEN_ADJUST_SHAPE_SLOT 6
#define SCREEN_ADJUST_FILTER_SLOT 8
#define SCREEN_ADJUST_INTENSITY_SLOT 9

#endif
