#ifndef POKKETSTATION_DESKTOP_RESOURCE_H
#define POKKETSTATION_DESKTOP_RESOURCE_H

#define IDI_MAINICON 100

#define IDR_MAINMENU 201
#define IDD_HWID 202
#define IDD_ADVANCED_COLORS 203
/* 204 was IDD_SHADOW_COLOR, a dialog for the sprite shadow color alone. That
   color is now one of the three IDD_ADVANCED_COLORS edits, since it only ever
   made sense picked against the scheme it sits on. */
#define IDD_REMAP_CONTROLS 205
#define IDD_CAPTURE_PROMPT 206
#define IDD_ABOUT 207
/* Device > IR Link > Host Over Network... and Connect Over Network... both use this one
   dialog. It holds one address field. The caption and the label text come from
   main.c, because the host side asks for a port to listen on and the client side
   asks for the address of a peer. */
#define IDD_IR_ADDRESS 208

#define IDC_HWID_EDIT 1001
#define IDC_PIXEL_HEX 1002
#define IDC_PIXEL_CHOOSE 1003
#define IDC_BG_HEX 1004
#define IDC_BG_CHOOSE 1005
#define IDC_SHADOW_HEX 1006
#define IDC_SHADOW_CHOOSE 1007
/* 1008 was IDC_SHADOW_RESET, which belonged to the retired IDD_SHADOW_COLOR. */
#define IDC_CAPTURE_PROMPT_TEXT 1009
/* 1010-1028 were IDC_REMAP_LABEL_BASE and IDC_REMAP_CHANGE_BASE, when the
   dialog had 9 rows. The two ranges are now at the end of this file. */
#define IDC_ABOUT_LINK 1030
#define IDC_ABOUT_VERSION 1031
#define IDC_ABOUT_LICENSE_LINK 1032
/* IDD_ADVANCED_COLORS' controls, beyond the three IDC_*_HEX/IDC_*_CHOOSE
   pairs above that make up its collapsible Custom Colors group.
   IDC_SCREEN_CHOOSE is the always-visible one-color pick; it writes the same
   IDC_BG_HEX field the group's own Choose button does, and additionally
   re-derives the other two colors from it. */
#define IDC_CUSTOM_TOGGLE 1033
#define IDC_CUSTOM_GROUP 1034
#define IDC_CUSTOM_PIXEL_LABEL 1035
#define IDC_CUSTOM_SHADOW_LABEL 1036
#define IDC_COLOR_PREVIEW 1037
#define IDC_REMATCH 1038
#define IDC_CUSTOM_BG_LABEL 1039
#define IDC_SCREEN_CHOOSE 1040
#define IDC_SHADOWS_ENABLE 1041
/* IDD_IR_ADDRESS: the one address field, and the label above it. main.c sets the
   label text, because the host side and the client side ask for different
   things. */
#define IDC_IR_ADDRESS_EDIT 1042
#define IDC_IR_ADDRESS_LABEL 1043

#define ID_FILE_OPEN_BIOS 1101
#define ID_FILE_OPEN_APP 1102
#define ID_FILE_EXIT 1103
#define ID_TOOLS_EDIT_HWID 1104
#define ID_TOOLS_REMAP_CONTROLS 1115
#define ID_HELP_ABOUT 1105
#define ID_VIEW_NATIVE_SIZE 1106
#define ID_VIEW_DOUBLE_SIZE 1107
#define ID_COLORS_STANDARD 1108
#define ID_COLORS_REVERSED 1109
#define ID_COLORS_CLASSIC 1110
#define ID_COLORS_ADVANCED 1111
/* 1112-1114 were the View > Sprite Shadows submenu (Enable, Disable, Shadow
   Color...). That submenu is gone: the toggle is a checkbox in
   IDD_ADVANCED_COLORS and the color is one of its three edits. */
#define ID_FILE_RESET 1116
/* 1117/1118 were ID_FILE_QUICK_SAVE/ID_FILE_QUICK_LOAD, back when there was a
   single save slot. The two ranges at the end of this file replaced them; the
   quick slot is now slot 0 of each. */
#define ID_IR_HOST 1119
#define ID_IR_CONNECT 1120
#define ID_IR_DISCONNECT 1121
/* Device > IR Link > Host Over Network... and Connect Over Network..., the TCP pair of
   ID_IR_HOST and ID_IR_CONNECT above. The two local items keep one fixed pipe
   name and need no dialog. These two ask for an address, thus they carry an
   ellipsis. ID_IR_DISCONNECT ends a link of either kind. */
#define ID_IR_HOST_NET 1149
#define ID_IR_CONNECT_NET 1150
/* Options > Date/Time Override. A radio set: exactly one is checked, and
   "Default" means the frontend does not interfere, so the BIOS's own system
   menus work normally.
   1122-1125 were ID_TOOLS_VOLUME_DEFAULT/LOUD/SOFT/MUTE, a matching group that
   pinned the PocketStation's own volume byte. Options > Sound > Volume below
   replaced it: an application-level volume covers what anyone actually wanted
   from it, without reaching into emulated RAM or needing a BIOS this project
   has traced. */
#define ID_TOOLS_DATETIME_DEFAULT 1126
#define ID_TOOLS_DATETIME_OS 1127
/* Options > Sound > Volume: this application's own output level, applied by the
   frontend to the PCM it queues, so it works on any BIOS.
   11 consecutive IDs, one per 10% step: ID_TOOLS_SOUND_BASE + percent / 10,
   with +0 (Mute) and +10 (100%) as the ends. Held as a range rather than named
   IDs so main.c converts both ways with arithmetic and no lookup table, the
   same shape as IDC_REMAP_LABEL_BASE above. */
#define ID_TOOLS_SOUND_BASE 1128
#define ID_TOOLS_SOUND_LAST 1138
/* Options > Sound > Speaker, the sibling of Volume above: which small-speaker
   response the frontend filters the DAC's output through, so it sounds like the
   hardware's own tiny speaker rather than like the raw signal on a full-range
   PC speaker. See SPEAKER_SIM_PRESETS in main.c.
   A contiguous range again, one ID per preset in SPEAKER_SIM_* order:
   ID_TOOLS_SPEAKER_BASE + preset, with +0 (Full Range) meaning no filtering. */
#define ID_TOOLS_SPEAKER_BASE 1139
#define ID_TOOLS_SPEAKER_LAST 1142
/* File > Save State and File > Load State, one ID per slot in each:
   ID_FILE_SAVE_SLOT_BASE + slot and ID_FILE_LOAD_SLOT_BASE + slot, for slot
   0..SAVE_SLOT_COUNT-1 (see main.c). Slot 0 is the quick slot, the one the
   remappable Save State/Load State keys act on; slots 1 and 2 are menu-only.
   Contiguous ranges rather than named IDs, the same shape as the two Sound
   groups above, so main.c converts an ID to a slot number with arithmetic. */
#define ID_FILE_SAVE_SLOT_BASE 1143
#define ID_FILE_SAVE_SLOT_LAST 1145
#define ID_FILE_LOAD_SLOT_BASE 1146
#define ID_FILE_LOAD_SLOT_LAST 1148
/* The Device menu: Pause, Frame Advance, Speed, and Rewind. Its IR Link
   submenu uses the ID_IR_* values above. Speed is a radio group in one
   contiguous range, in the order of EMU_SPEEDS in main.c: 25%, 50%, 100%,
   200%, 400%, Unlimited. */
#define ID_EMU_PAUSE 1151
#define ID_EMU_FRAME_ADVANCE 1152
#define ID_EMU_REWIND_ENABLE 1153
#define ID_EMU_SPEED_BASE 1154
#define ID_EMU_SPEED_LAST 1159
/* File > Movie. */
#define ID_MOVIE_RECORD 1160
#define ID_MOVIE_PLAY 1161
#define ID_MOVIE_STOP 1162
#define ID_MOVIE_READ_ONLY 1163

/* IDC_REMAP_LABEL_BASE and IDC_REMAP_CHANGE_BASE each reserve 20 consecutive
   IDs. 14 are in use, one for each remappable action.
   The action order is fixed: Up, Down, Left, Right, Fire, Create-Debug-Log,
   Reset, Quick-Save-State, Quick-Load-State, Pause, Frame-Advance,
   Fast-Forward, Rewind, Movie-Read-Only.
   See button_scancodes in main.c for this order.
   IDC_REMAP_LABEL_BASE+i shows the current key name of binding i.
   IDC_REMAP_CHANGE_BASE+i is the "Change..." button of that row. */
#define IDC_REMAP_LABEL_BASE 1200
#define IDC_REMAP_CHANGE_BASE 1220

#endif
