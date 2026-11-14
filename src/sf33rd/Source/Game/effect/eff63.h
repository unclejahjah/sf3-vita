#ifndef EFF63_H
#define EFF63_H

#include "structs.h"
#include "types.h"

void effect_63_move(WORK_Other_CONN* ewk);
s32 effect_63_init(u8 dir_old, s16 sync_bg, s16 master_player, s16 letter_type, s16 cursor_index);

/// The same values in the small font, for the Screen Adjust page that has a picture shape row.
/// `letter_type` 4 shows the picture shape
s32 effect_63_init_compact(u8 dir_old, s16 sync_bg, s16 master_player, s16 letter_type, s16 cursor_index);

/// The entry of `Convert_Buff[2][0]` that holds the picture shape while the Screen Adjust page is open
#define SCREEN_ADJUST_SHAPE_SLOT 6

#endif
