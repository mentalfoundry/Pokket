#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "libretro.h"
#include "psemu/psemu.h"

/* The project() line of the root CMakeLists.txt gives POKKET_VERSION. */
#ifndef POKKET_VERSION
#error "POKKET_VERSION is not defined. Build this core with CMake."
#endif

#define CONTENT_EXTENSIONS "mcr|mcs|pss|mcd|bin|gme"

/* The cycle budget of one frame at PSEMU_ASSUMED_CPU_HZ. See docs/hardware-notes.md. */
#define FRAME_CYCLES 33000u

/* The IR link subsystem operates two units in this one core, as two PocketStation units that a user
   holds together. A usual load operates unit A only. g_units[0] is unit A, and g_units[1] is unit B.
   See retro_load_game_special. */
#define LINK_UNITS 2u
#define UNIT_A 0u
#define UNIT_B 1u

static psemu_t *g_units[LINK_UNITS];
static unsigned g_unit_count = 1;

/* The space between the two screens in link mode, in LCD pixels. Without it, the white background of
   one screen touches the white background of the other screen, and the edge of each screen is not
   visible. */
#define LINK_GAP_PIXELS 2
#define LINK_GAP_COLOR 0xFF808080u
#define FRAME_MAX_WIDTH (PSEMU_LCD_WIDTH * 2 + LINK_GAP_PIXELS)
#define FRAME_MAX_HEIGHT (PSEMU_LCD_HEIGHT * 2 + LINK_GAP_PIXELS)
static uint32_t g_framebuffer[FRAME_MAX_WIDTH * FRAME_MAX_HEIGHT];

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

/* The identity of each card immediately after the load, and before the frontend can write its own
   save data over flash. See check_savedata_identity. */
static uint32_t g_loaded_identity[LINK_UNITS];
static bool g_identity_check_pending[LINK_UNITS];

/* The IR link subsystem.

   The relay uses the same method as tools/ir_probe.c, which verifies an IR transfer of two real apps
   with two instances in one process (see "Verified transfers" in docs/hardware-notes.md). Each
   retro_run advances both units by FRAME_CYCLES. It then moves each TX edge of one unit into the RX
   queue of the other unit.

   Emulated time is the shared clock of the two units. The IR clock of a unit counts the reference
   cycles since its last psemu_reset. This core resets the two units together, at the load and at
   retro_reset, and a save state holds both units. Thus a timestamp on the IR clock of one unit gives
   the same moment on the IR clock of the other unit, and the relay needs no clock conversion. A
   psemu_run call can go past its budget by part of one instruction. That does not change the result,
   because each clock counts the time since the reset, and not the sum of the budgets.

   The desktop frontend relays between two processes with the host wall clock (frontends/desktop/
   ir_link.h). It must measure an offset between two clocks, and it must hold that offset constant
   through each message. This core does not use the wall clock, because both units are in one process.
   Thus the link stays correct at each speed of the frontend: fast-forward, pause, frame advance,
   rewind, and run-ahead. The save state holds both units, and it holds each edge that is in transit,
   thus the link is deterministic. */
#define GAME_TYPE_IR_LINK 0x101u

/* The libretro memory IDs of the two cards in link mode. The bits above bit 7 select the unit, and the
   low bits keep RETRO_MEMORY_SAVE_RAM. */
#define MEMORY_UNIT_A_FLASH ((1u << 8) | RETRO_MEMORY_SAVE_RAM)
#define MEMORY_UNIT_B_FLASH ((2u << 8) | RETRO_MEMORY_SAVE_RAM)

/* The time by which each relayed edge is scheduled ahead of the IR clock of the receiving unit.
   An edge that arrives with a timestamp in the past of the receiver is released at the next CPU step,
   together with each other late edge. The space between those edges then becomes zero, and a
   bit-banged message stops decoding. See "A relay needs a playout delay" in docs/hardware-notes.md.
   In this core an edge is at most one frame old when it arrives: FRAME_CYCLES at PSEMU_ASSUMED_CPU_HZ,
   which is 31250us. This value is the default of tools/ir_probe.c, with which both verified transfers
   complete at a relay period of one frame. */
#define LINK_PLAYOUT_DELAY_US 100000ull

/* The unit that controller 1 operates in link mode. L selects unit A, and R selects unit B. Thus one
   user with one controller can operate both sides of an IR exchange. Controller 2 always operates
   unit B. */
static unsigned g_port0_unit = UNIT_A;

/* Set at the load of a link. The first retro_run then shows the position of each unit and the
   controls, because the RetroArch menu gives neither one. See show_link_start_message. */
static bool g_link_start_message_pending;

/* The audio of each unit, before the output step mixes the two streams. Each unit gives samples at
   PSEMU_AUDIO_SAMPLE_RATE_HZ, thus the two counts of one frame are equal or almost equal. The output
   step sends the smaller count, and the remainder stays here for the next frame. */
#define LINK_AUDIO_PENDING 2048u
static int16_t g_audio_pending[LINK_UNITS][LINK_AUDIO_PENDING];
static uint32_t g_audio_pending_count[LINK_UNITS];

/* The save state of link mode is this header, then the state of unit A, then the state of unit B. */
#define LINK_STATE_MAGIC 0x4B4C4B50u /* 'PKLK' */
#define LINK_STATE_VERSION 1u
#define LINK_STATE_HEADER_SIZE 8u

/* Core options. Each option has an effect in link mode only. */
#define OPTION_LINK_LAYOUT "pokket_link_layout"
#define OPTION_LINK_AUDIO "pokket_link_audio"
#define OPTION_LINK_UNIT_B_ID "pokket_link_unit_b_id"

static const struct retro_variable g_variables[] = {
    { OPTION_LINK_LAYOUT, "IR link: screen layout; Side by side|Stacked" },
    { OPTION_LINK_AUDIO, "IR link: audio; Both units|Unit A|Unit B|Unit A left, unit B right" },
    { OPTION_LINK_UNIT_B_ID, "IR link: hardware ID of unit B; Different from unit A|Same as unit A" },
    { NULL, NULL },
};

typedef enum { LAYOUT_SIDE_BY_SIDE, LAYOUT_STACKED } link_layout_t;
typedef enum { AUDIO_BOTH, AUDIO_UNIT_A, AUDIO_UNIT_B, AUDIO_SPLIT } link_audio_t;

static link_layout_t g_link_layout = LAYOUT_SIDE_BY_SIDE;
static link_audio_t g_link_audio = AUDIO_BOTH;

static const struct retro_subsystem_memory_info g_unit_a_memory[] = {
    { "srm", MEMORY_UNIT_A_FLASH },
};

static const struct retro_subsystem_memory_info g_unit_b_memory[] = {
    { "srm", MEMORY_UNIT_B_FLASH },
};

/* The extension "srm" gives each card the same save file in link mode and in a usual load. RetroArch
   makes the name of a subsystem save file from the name of its content file. Thus the result of a
   trade is in the card at the next usual load.

   The description of each card is the only text that tells a user what to do next. RetroArch shows
   one menu item, "Load <desc of the subsystem>", for each card, and the same item again for the
   start. Below that item, it shows "Current Content: <desc of the next card>". Thus each description
   gives the step, and not only the name of the card. */
static const struct retro_subsystem_rom_info g_link_roms[] = {
    { "Step 1 of 3: select the card for unit A", CONTENT_EXTENSIONS, false, false, true, g_unit_a_memory, 1 },
    { "Step 2 of 3: select the card for unit B", CONTENT_EXTENSIONS, false, false, true, g_unit_b_memory, 1 },
};

static const struct retro_subsystem_info g_subsystems[] = {
    { "IR Link (2 PocketStations)", "ir_link", g_link_roms, LINK_UNITS, GAME_TYPE_IR_LINK },
    { NULL, NULL, NULL, 0, 0 },
};

void retro_set_environment(retro_environment_t cb) {
    environ_cb = cb;
    environ_cb(RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO, (void *)g_subsystems);
    environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)g_variables);
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }

void retro_init(void) {
    struct retro_log_callback log;
    g_units[UNIT_A] = psemu_create();
    /* This interface is optional in the libretro specification. A frontend does not have to supply
       it, thus each use of the interface has a guard. */
    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log)) {
        log_cb = log.log;
    }
}

static void destroy_unit_b(void) {
    psemu_destroy(g_units[UNIT_B]);
    g_units[UNIT_B] = NULL;
    g_unit_count = 1;
}

void retro_deinit(void) {
    destroy_unit_b();
    psemu_destroy(g_units[UNIT_A]);
    g_units[UNIT_A] = NULL;
}

unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name = "Pokket";
    info->library_version = POKKET_VERSION;
    info->valid_extensions = CONTENT_EXTENSIONS;
    info->need_fullpath = false;
    info->block_extract = false;
}

static void get_geometry(struct retro_game_geometry *geometry) {
    if (g_unit_count == 1) {
        geometry->base_width = PSEMU_LCD_WIDTH;
        geometry->base_height = PSEMU_LCD_HEIGHT;
        geometry->max_width = PSEMU_LCD_WIDTH;
        geometry->max_height = PSEMU_LCD_HEIGHT;
    } else {
        if (g_link_layout == LAYOUT_STACKED) {
            geometry->base_width = PSEMU_LCD_WIDTH;
            geometry->base_height = FRAME_MAX_HEIGHT;
        } else {
            geometry->base_width = FRAME_MAX_WIDTH;
            geometry->base_height = PSEMU_LCD_HEIGHT;
        }
        /* The maximum covers both layouts, because a change of the layout option during a session
           cannot change the maximum. */
        geometry->max_width = FRAME_MAX_WIDTH;
        geometry->max_height = FRAME_MAX_HEIGHT;
    }
    geometry->aspect_ratio = (float)geometry->base_width / (float)geometry->base_height;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    get_geometry(&info->geometry);
    info->timing.fps = 32.0;
    info->timing.sample_rate = (double)PSEMU_AUDIO_SAMPLE_RATE_HZ;
}

static const char *get_variable(const char *key) {
    struct retro_variable var;
    var.key = key;
    var.value = NULL;
    if (!environ_cb || !environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) {
        return NULL;
    }
    return var.value;
}

/* Reads the options that can change during a session. A change of the layout in link mode also
   changes the geometry. */
static void update_options(void) {
    const char *value;
    link_layout_t old_layout = g_link_layout;

    value = get_variable(OPTION_LINK_LAYOUT);
    g_link_layout = (value && strcmp(value, "Stacked") == 0) ? LAYOUT_STACKED : LAYOUT_SIDE_BY_SIDE;

    value = get_variable(OPTION_LINK_AUDIO);
    if (value && strcmp(value, "Unit A") == 0) {
        g_link_audio = AUDIO_UNIT_A;
    } else if (value && strcmp(value, "Unit B") == 0) {
        g_link_audio = AUDIO_UNIT_B;
    } else if (value && strcmp(value, "Unit A left, unit B right") == 0) {
        g_link_audio = AUDIO_SPLIT;
    } else {
        g_link_audio = AUDIO_BOTH;
    }

    if (g_unit_count == LINK_UNITS && g_link_layout != old_layout && environ_cb) {
        struct retro_game_geometry geometry;
        get_geometry(&geometry);
        environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
    }
}

static void show_message(const char *text, unsigned frames) {
    struct retro_message msg;
    msg.msg = text;
    msg.frames = frames;
    if (environ_cb) {
        environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
    }
}

/* Resets both units together in link mode. See the comment on the IR link subsystem above. */
void retro_reset(void) {
    unsigned unit;
    for (unit = 0; unit < g_unit_count; unit++) {
        psemu_reset(g_units[unit]);
    }
}

static uint32_t read_buttons(unsigned port) {
    uint32_t buttons = 0;
    if (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP)) buttons |= PSEMU_BUTTON_UP;
    if (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN)) buttons |= PSEMU_BUTTON_DOWN;
    if (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT)) buttons |= PSEMU_BUTTON_LEFT;
    if (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) buttons |= PSEMU_BUTTON_RIGHT;
    if (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A)) buttons |= PSEMU_BUTTON_FIRE;
    return buttons;
}

/* Selects the unit that controller 1 operates. See g_port0_unit. */
static void select_port0_unit(void) {
    unsigned unit = g_port0_unit;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L)) {
        unit = UNIT_A;
    } else if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R)) {
        unit = UNIT_B;
    }
    if (unit != g_port0_unit) {
        g_port0_unit = unit;
        show_message(unit == UNIT_A ? "Controller 1 operates unit A." : "Controller 1 operates unit B.", 64);
    }
}

static void update_input(void) {
    uint32_t port0;
    if (!input_poll_cb || !input_state_cb) {
        return;
    }
    input_poll_cb();
    port0 = read_buttons(0);
    if (g_unit_count == 1) {
        psemu_set_buttons(g_units[UNIT_A], port0);
        return;
    }
    select_port0_unit();
    psemu_set_buttons(g_units[UNIT_A], g_port0_unit == UNIT_A ? port0 : 0u);
    psemu_set_buttons(g_units[UNIT_B], read_buttons(1) | (g_port0_unit == UNIT_B ? port0 : 0u));
}

/* Writes the screen of one unit into the framebuffer at `dst`. `pitch` is the width of one row of the
   framebuffer, in pixels. */
static void convert_framebuffer(const psemu_t *ps, uint32_t *dst, unsigned pitch) {
    const uint8_t *fb = psemu_get_framebuffer(ps);
    for (int row = 0; row < PSEMU_LCD_HEIGHT; row++) {
        for (int col = 0; col < PSEMU_LCD_WIDTH; col++) {
            int byte_index = row * PSEMU_LCD_STRIDE + col / 8;
            int bit_index = col % 8;
            int on = (fb[byte_index] >> bit_index) & 1;
            dst[row * pitch + col] = on ? 0xFF000000u : 0xFFFFFFFFu;
        }
    }
}

static void submit_video(void) {
    struct retro_game_geometry geometry;
    unsigned pitch;
    get_geometry(&geometry);
    pitch = geometry.base_width;
    if (g_unit_count == LINK_UNITS) {
        for (unsigned i = 0; i < geometry.base_width * geometry.base_height; i++) {
            g_framebuffer[i] = LINK_GAP_COLOR;
        }
        convert_framebuffer(g_units[UNIT_A], g_framebuffer, pitch);
        if (g_link_layout == LAYOUT_STACKED) {
            convert_framebuffer(g_units[UNIT_B], g_framebuffer + (PSEMU_LCD_HEIGHT + LINK_GAP_PIXELS) * pitch, pitch);
        } else {
            convert_framebuffer(g_units[UNIT_B], g_framebuffer + PSEMU_LCD_WIDTH + LINK_GAP_PIXELS, pitch);
        }
    } else {
        convert_framebuffer(g_units[UNIT_A], g_framebuffer, pitch);
    }
    video_cb(g_framebuffer, geometry.base_width, geometry.base_height, pitch * sizeof(uint32_t));
}

static void submit_audio_single(void) {
    int16_t mono[512];
    int16_t stereo[512 * 2];
    uint32_t n;
    while ((n = psemu_get_audio_samples(g_units[UNIT_A], mono, sizeof(mono) / sizeof(mono[0]))) > 0) {
        for (uint32_t i = 0; i < n; i++) {
            stereo[i * 2 + 0] = mono[i];
            stereo[i * 2 + 1] = mono[i];
        }
        audio_batch_cb(stereo, n);
    }
}

static int16_t mix_samples(int16_t a, int16_t b) {
    int32_t sum = (int32_t)a + (int32_t)b;
    if (sum > 32767) {
        return 32767;
    }
    if (sum < -32768) {
        return -32768;
    }
    return (int16_t)sum;
}

static void submit_audio_link(void) {
    int16_t stereo[512 * 2];
    uint32_t n, done, unit;
    for (unit = 0; unit < LINK_UNITS; unit++) {
        uint32_t count = g_audio_pending_count[unit];
        g_audio_pending_count[unit] +=
            psemu_get_audio_samples(g_units[unit], g_audio_pending[unit] + count, LINK_AUDIO_PENDING - count);
    }
    n = g_audio_pending_count[UNIT_A] < g_audio_pending_count[UNIT_B] ? g_audio_pending_count[UNIT_A]
                                                                      : g_audio_pending_count[UNIT_B];
    for (done = 0; done < n;) {
        uint32_t chunk = n - done;
        if (chunk > 512) {
            chunk = 512;
        }
        for (uint32_t i = 0; i < chunk; i++) {
            int16_t a = g_audio_pending[UNIT_A][done + i];
            int16_t b = g_audio_pending[UNIT_B][done + i];
            int16_t left, right;
            switch (g_link_audio) {
            case AUDIO_UNIT_A:
                left = right = a;
                break;
            case AUDIO_UNIT_B:
                left = right = b;
                break;
            case AUDIO_SPLIT:
                left = a;
                right = b;
                break;
            default:
                left = right = mix_samples(a, b);
                break;
            }
            stereo[i * 2 + 0] = left;
            stereo[i * 2 + 1] = right;
        }
        audio_batch_cb(stereo, chunk);
        done += chunk;
    }
    for (unit = 0; unit < LINK_UNITS; unit++) {
        g_audio_pending_count[unit] -= n;
        memmove(g_audio_pending[unit], g_audio_pending[unit] + n, g_audio_pending_count[unit] * sizeof(int16_t));
    }
}

static void submit_audio(void) {
    if (!audio_batch_cb) {
        return;
    }
    if (g_unit_count == LINK_UNITS) {
        submit_audio_link();
    } else {
        submit_audio_single();
    }
}

/* Moves each TX edge of `from` into the RX queue of `to`. The two IR clocks agree (see the comment on
   the IR link subsystem above), thus only LINK_PLAYOUT_DELAY_US changes the timestamp. */
static void relay_edges(psemu_t *from, psemu_t *to) {
    psemu_ir_edge_t edge;
    while (psemu_ir_pop_tx_edge(from, &edge)) {
        psemu_ir_push_rx_edge(to, edge.timestamp_us + LINK_PLAYOUT_DELAY_US, edge.level);
    }
}

/* Approximately 6 seconds, at the 32 frames each second of this core. */
#define IDENTITY_WARNING_FRAMES (32 * 6)

/* A frontend copies save data over flash after retro_load_game returns, and before the first
   retro_run. The frontend does no validation: the save file with the name of this content always
   wins. This behavior is usually correct, because that file is the same card at a later time. But
   when the behavior is incorrect, the frontend discards the bytes of the content and gives no error.
   This condition does occur. The save data of this core IS a card image (see
   retro_get_memory_data), thus the documentation tells users to open it in external memory-card
   tools. If a user edits a card in that way, and then loads the card here while an old save file
   from an earlier session is still present, the edit is lost with no message.

   psemu_content_identity_hash gives an answer that is near to this question. The hash covers the
   saves on a card, and it does not include the bytes that an app writes when it saves. Thus a usual
   session still gives the same hash. Examples of a usual session are an app that writes its own
   progress, and an app that edits the PS1 save next to it.

   The hash is not exact. Know this difference before you report it as a fault: the hash also changes
   when the set of files on the card changes. Thus an app that MAKES or DELETES a save file causes
   this warning at the next load, even for the correct card. This condition is rare for PocketStation
   apps, which stay in their own blocks and write in them. The result is one incorrect warning, and
   no data loss. Thus this is the correct compromise against a failure to find the real condition.

   This code hashes flash on both sides, before and after. It does not compare the loaded file against
   flash. A .mcs or .pss file and the card that this emulator synthesizes around it hash in different
   domains: one is a file, and the other is a card. Thus a comparison of those two has no meaning. A
   comparison of flash before against flash after uses the same domain, for all three content kinds.

   The save data still wins. To refuse the save data can discard the real progress of a session
   because of an incorrect warning, which is a much worse failure. This code only makes the
   replacement visible. In link mode, the frontend copies the save data of each card, and this code
   examines each unit. Returns true if it showed a warning. */
static bool check_savedata_identity(unsigned unit) {
    static char text[128];
    if (psemu_content_identity_hash(psemu_flash_data(g_units[unit]), PSEMU_FLASH_SIZE) == g_loaded_identity[unit]) {
        return false;
    }
    if (log_cb) {
        log_cb(RETRO_LOG_WARN,
               "[Pokket] Save data holds a different memory card than the loaded content%s, and "
               "has replaced it. If you edited this card outside RetroArch, delete its .srm and "
               "reload.\n",
               g_unit_count == 1 ? "" : (unit == UNIT_A ? " of unit A" : " of unit B"));
    }
    if (g_unit_count == 1) {
        snprintf(text, sizeof(text), "Save data is from a different memory card, and has replaced the loaded content.");
    } else {
        snprintf(text, sizeof(text), "Save data of unit %c is from a different memory card, and has replaced the loaded content.",
                 unit == UNIT_A ? 'A' : 'B');
    }
    show_message(text, IDENTITY_WARNING_FRAMES);
    return true;
}

/* Approximately 5 seconds. */
#define LINK_START_MESSAGE_FRAMES (32 * 5)

/* Tells the user where each unit is, and how one controller operates both units. */
static void show_link_start_message(void) {
    show_message(g_link_layout == LAYOUT_STACKED
                     ? "IR link: unit A is at the top, and unit B is at the bottom. On controller 1, L selects unit A and R selects unit B."
                     : "IR link: unit A is on the left, and unit B is on the right. On controller 1, L selects unit A and R selects unit B.",
                 LINK_START_MESSAGE_FRAMES);
}

void retro_run(void) {
    unsigned unit;
    bool options_updated = false;
    bool warned = false;
    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &options_updated) && options_updated) {
        update_options();
    }
    for (unit = 0; unit < g_unit_count; unit++) {
        if (g_identity_check_pending[unit]) {
            g_identity_check_pending[unit] = false;
            warned = check_savedata_identity(unit) || warned;
        }
    }
    /* A warning about save data is more important, and a second message can replace it. */
    if (g_link_start_message_pending) {
        g_link_start_message_pending = false;
        if (!warned) {
            show_link_start_message();
        }
    }
    update_input();
    for (unit = 0; unit < g_unit_count; unit++) {
        psemu_run(g_units[unit], FRAME_CYCLES);
    }
    if (g_unit_count == LINK_UNITS) {
        relay_edges(g_units[UNIT_A], g_units[UNIT_B]);
        relay_edges(g_units[UNIT_B], g_units[UNIT_A]);
    }
    submit_video();
    submit_audio();
}

/* The BIOS is copyrighted Sony firmware. This project does not supply it.
   Make a dump of the 16KB BIOS ROM from real hardware that you own.
   A libretro frontend usually loads such a file from its system directory. */
static bool read_bios(uint8_t bios[PSEMU_BIOS_SIZE]) {
    const char *system_dir = NULL;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) || !system_dir) {
        return false;
    }
    char path[4096];
    snprintf(path, sizeof(path), "%s/pocketstation.bin", system_dir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    size_t bytes_read = fread(bios, 1, PSEMU_BIOS_SIZE, f);
    fclose(f);
    return bytes_read == PSEMU_BIOS_SIZE;
}

static bool start_unit(unsigned unit, const uint8_t *bios, const struct retro_game_info *game) {
    psemu_t *ps = g_units[unit];
    if (!ps || !game || !game->data) {
        return false;
    }
    if (psemu_load_bios(ps, bios, PSEMU_BIOS_SIZE) != PSEMU_OK) {
        return false;
    }
    /* This emulator examines the file content. It does not use the file extension.
       See the comment on psemu_load_content for the exact order of priority. */
    if (psemu_load_content(ps, (const uint8_t *)game->data, game->size) != PSEMU_OK) {
        return false;
    }
    psemu_reset(ps);
    /* Record the hash now, while flash holds only the data from the content. */
    g_loaded_identity[unit] = psemu_content_identity_hash(psemu_flash_data(ps), PSEMU_FLASH_SIZE);
    g_identity_check_pending[unit] = true;
    return true;
}

static bool set_pixel_format(void) {
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    return environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
}

bool retro_load_game(const struct retro_game_info *game) {
    uint8_t bios[PSEMU_BIOS_SIZE];
    if (!set_pixel_format()) {
        return false;
    }
    if (!read_bios(bios)) {
        return false;
    }
    return start_unit(UNIT_A, bios, game);
}

/* Gives the file name of `path` without its directory and without its extension. */
static void save_name_of(const char *path, const char **out_name, size_t *out_length) {
    const char *name = path;
    const char *dot = NULL;
    const char *p;
    for (p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
            dot = NULL;
        } else if (*p == '.') {
            dot = p;
        }
    }
    *out_name = name;
    *out_length = dot ? (size_t)(dot - name) : (size_t)(p - name);
}

/* RetroArch makes the name of each subsystem save file from the name of its content file, without
   the directory and without the extension. When a save directory is set, two content files with the
   same name in different directories thus get one save file. The two units then start from the same
   save data, and the save of one unit replaces the save of the other unit. This function finds that
   condition before the load. The comparison ignores case, because the file system of Windows ignores
   case. */
static bool units_share_save_name(const struct retro_game_info *info) {
    const char *name_a, *name_b;
    size_t length_a, length_b, i;
    if (!info[UNIT_A].path || !info[UNIT_B].path) {
        return false;
    }
    save_name_of(info[UNIT_A].path, &name_a, &length_a);
    save_name_of(info[UNIT_B].path, &name_b, &length_b);
    if (length_a != length_b) {
        return false;
    }
    for (i = 0; i < length_a; i++) {
        if (tolower((unsigned char)name_a[i]) != tolower((unsigned char)name_b[i])) {
            return false;
        }
    }
    return true;
}

/* Unit B gets the hardware ID of unit A with the high byte increased by 1, unless the option selects
   the same ID. Two real units have different IDs, and the transfers that tools/ir_probe.c verifies use
   two different IDs. The companion app that reads F_SN removes the high byte (see
   psemu_set_hardware_id in psemu.h), thus unit B keeps the rank of unit A in that app. */
static uint32_t unit_b_hardware_id(void) {
    const char *value = get_variable(OPTION_LINK_UNIT_B_ID);
    uint32_t id_a = psemu_get_hardware_id(g_units[UNIT_A]);
    if (value && strcmp(value, "Same as unit A") == 0) {
        return id_a;
    }
    return (id_a & 0x00FFFFFFu) | ((((id_a >> 24) + 1u) & 0xFFu) << 24);
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) {
    uint8_t bios[PSEMU_BIOS_SIZE];
    if (game_type != GAME_TYPE_IR_LINK || num_info != LINK_UNITS || !info) {
        return false;
    }
    if (!set_pixel_format()) {
        return false;
    }
    if (units_share_save_name(info)) {
        if (log_cb) {
            log_cb(RETRO_LOG_ERROR, "[Pokket] IR link: unit A and unit B have the same file name, thus they "
                                    "have the same save file. Give one card a different file name.\n");
        }
        show_message("Unit A and unit B have the same file name. Give one card a different file name.",
                     IDENTITY_WARNING_FRAMES);
        return false;
    }
    if (!read_bios(bios)) {
        return false;
    }
    g_units[UNIT_B] = psemu_create();
    if (!g_units[UNIT_B]) {
        return false;
    }
    /* Before the load of the content. See psemu_set_hardware_id. */
    psemu_set_hardware_id(g_units[UNIT_B], unit_b_hardware_id());
    if (!start_unit(UNIT_A, bios, &info[UNIT_A]) || !start_unit(UNIT_B, bios, &info[UNIT_B])) {
        destroy_unit_b();
        g_identity_check_pending[UNIT_A] = false;
        g_identity_check_pending[UNIT_B] = false;
        return false;
    }
    /* Before g_unit_count changes, thus the read gives no SET_GEOMETRY call during the load. The
       frontend gets the geometry from retro_get_system_av_info after this function. */
    update_options();
    g_unit_count = LINK_UNITS;
    g_port0_unit = UNIT_A;
    g_link_start_message_pending = true;
    g_audio_pending_count[UNIT_A] = 0;
    g_audio_pending_count[UNIT_B] = 0;
    return true;
}

void retro_unload_game(void) {
    destroy_unit_b();
    g_link_start_message_pending = false;
    g_identity_check_pending[UNIT_A] = false;
    g_identity_check_pending[UNIT_B] = false;
}

unsigned retro_get_region(void) {
    return RETRO_REGION_NTSC;
}

size_t retro_serialize_size(void) {
    if (g_unit_count == LINK_UNITS) {
        return LINK_STATE_HEADER_SIZE + psemu_state_size(g_units[UNIT_A]) + psemu_state_size(g_units[UNIT_B]);
    }
    return psemu_state_size(g_units[UNIT_A]);
}

bool retro_serialize(void *data, size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t size_a, size_b;
    uint32_t magic = LINK_STATE_MAGIC, version = LINK_STATE_VERSION;
    if (g_unit_count == 1) {
        return psemu_save_state(g_units[UNIT_A], data, size) == PSEMU_OK;
    }
    size_a = psemu_state_size(g_units[UNIT_A]);
    size_b = psemu_state_size(g_units[UNIT_B]);
    if (size < LINK_STATE_HEADER_SIZE + size_a + size_b) {
        return false;
    }
    memcpy(out, &magic, 4);
    memcpy(out + 4, &version, 4);
    out += LINK_STATE_HEADER_SIZE;
    if (psemu_save_state(g_units[UNIT_A], out, size_a) != PSEMU_OK) {
        return false;
    }
    return psemu_save_state(g_units[UNIT_B], out + size_a, size_b) == PSEMU_OK;
}

bool retro_unserialize(const void *data, size_t size) {
    const uint8_t *in = (const uint8_t *)data;
    size_t size_a, size_b;
    uint32_t magic, version;
    if (g_unit_count == 1) {
        return psemu_load_state(g_units[UNIT_A], data, size) == PSEMU_OK;
    }
    size_a = psemu_state_size(g_units[UNIT_A]);
    size_b = psemu_state_size(g_units[UNIT_B]);
    if (size < LINK_STATE_HEADER_SIZE + size_a + size_b) {
        return false;
    }
    memcpy(&magic, in, 4);
    memcpy(&version, in + 4, 4);
    if (magic != LINK_STATE_MAGIC || version != LINK_STATE_VERSION) {
        return false;
    }
    in += LINK_STATE_HEADER_SIZE;
    return psemu_load_state(g_units[UNIT_A], in, size_a) == PSEMU_OK &&
           psemu_load_state(g_units[UNIT_B], in + size_a, size_b) == PSEMU_OK;
}

void retro_cheat_reset(void) {
}

void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void)index;
    (void)enabled;
    (void)code;
}

/* SAVE_RAM is the full 128KB FLASH2 image. This region is what keeps the progress of an app. Before
   this code, nothing continued after a session except a manual save state, and the saves of a session
   were lost at exit.
   Flash is the correct region, and a smaller region is not. The output of a PocketStation app is
   frequently an edit to the PS1 save of the game in a different block of the same card, and not the
   state of the app itself (see docs/app-notes.md). Thus a smaller region discards the exact edits
   that users need.

   THE SAVE FILE THAT THIS PRODUCES IS A .mcd FILE, BYTE FOR BYTE. This behavior is deliberate, and it
   depends on this region staying a flat PSEMU_FLASH_SIZE region. See the contract of
   psemu_flash_data, and the saves section of docs/libretro_readme.md. That section tells users to
   open the save file directly in external memory-card tools.
   All three content kinds operate this way, because a loaded .mcs or .pss file runs inside a full
   card that this emulator synthesizes around it.

   SYSTEM_RAM is work RAM. This core supplies it for the cheat-search and memory-viewer functions of a
   host. A frontend does not keep this region, which is correct: it is live state, and not save
   data.

   In link mode, MEMORY_UNIT_A_FLASH and MEMORY_UNIT_B_FLASH give the flash of each unit, with the same
   contract as SAVE_RAM. The standard IDs then give no region, because each one is unclear between
   the two units. */
void *retro_get_memory_data(unsigned id) {
    if (!g_units[UNIT_A]) {
        return NULL;
    }
    if (g_unit_count == LINK_UNITS) {
        switch (id) {
        case MEMORY_UNIT_A_FLASH:
            return psemu_flash_data(g_units[UNIT_A]);
        case MEMORY_UNIT_B_FLASH:
            return psemu_flash_data(g_units[UNIT_B]);
        default:
            return NULL;
        }
    }
    switch (id) {
    case RETRO_MEMORY_SAVE_RAM:
        return psemu_flash_data(g_units[UNIT_A]);
    case RETRO_MEMORY_SYSTEM_RAM:
        return psemu_ram_data(g_units[UNIT_A]);
    default:
        return NULL;
    }
}

size_t retro_get_memory_size(unsigned id) {
    if (g_unit_count == LINK_UNITS) {
        switch (id) {
        case MEMORY_UNIT_A_FLASH:
        case MEMORY_UNIT_B_FLASH:
            return PSEMU_FLASH_SIZE;
        default:
            return 0;
        }
    }
    switch (id) {
    case RETRO_MEMORY_SAVE_RAM:
        return PSEMU_FLASH_SIZE;
    case RETRO_MEMORY_SYSTEM_RAM:
        return PSEMU_RAM_SIZE;
    default:
        return 0;
    }
}
