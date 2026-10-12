/* Tests of the IR link subsystem of the libretro core.

   This file includes libretro.c directly, thus it can examine the static state of the core: the two
   units, the latched clock offset, and the selected unit of controller 1. A small mock frontend below
   gives the environment callback, the video callback, the audio callback, and the input callbacks.

   THESE TESTS NEED A BIOS DUMP, with the name pocketstation.bin in testdata/. The repository excludes
   each dump. This suite skips itself with exit code 77 when it finds no dump. CTest reports that code
   as a skip.

   The relay test writes the IR registers of unit A directly, in the same manner as tests/ir_test.c. It
   does not use a real IR app. tools/ir_probe.c holds the verification of real transfers. This test
   confirms that the core moves each edge to the other unit at the correct time, with the correct
   space between the edges.

   Each check in this file must fail when its contract is broken. A change to the relay, to the
   playout delay, or to the save state that this file does not find is a gap in this file. */

#include <stdlib.h>

#include "libretro.c"
#include "psemu_internal.h"

#define SKIP_EXIT_CODE 77

#define IRDA_MODE (PSEMU_IR_BASE + 0x0u)
#define IRDA_DATA (PSEMU_IR_BASE + 0x4u)
#define TX_ACTIVE_MODE (IR_MODE_IFMODE | IR_MODE_BFLT)
#define RX_ACTIVE_MODE 0u /* receive, with the BFLT filter enabled */

/* LINK_PLAYOUT_DELAY_US in reference cycles. */
#define PLAYOUT_DELAY_CYCLES (((uint64_t)LINK_PLAYOUT_DELAY_US * PSEMU_ASSUMED_CPU_HZ) / 1000000ull)

/* The conversion between the reference-cycle clock and microseconds at the public interface of the
   core rounds down two times, one time in each direction. */
#define CONVERSION_TOLERANCE_CYCLES 2u

/* ir.c delays a falling TX edge by IR_TX_FALL_STRETCH_CYCLES (200) at most. */
#define TX_FALL_STRETCH_MAX_CYCLES 200u

static int g_failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                          \
        }                                                                          \
    } while (0)

/* The mock frontend. */
static const struct retro_subsystem_info *g_seen_subsystems;
static const struct retro_variable *g_seen_variables;
static const char *g_option_layout;
static const char *g_option_unit_b_id;
static bool g_options_changed;
static struct retro_game_geometry g_seen_geometry;
static int g_geometry_calls;
static unsigned g_video_width, g_video_height;
static size_t g_video_pitch;
static const uint32_t *g_video_data;
static size_t g_audio_frames;
static uint32_t g_pad[2]; /* one bit for each RETRO_DEVICE_ID_JOYPAD_* value, for ports 0 and 1 */
static const char *g_last_message;
static int g_message_count;

static uint8_t g_card[PSEMU_FLASH_SIZE];

static bool mock_environment(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
        g_seen_subsystems = (const struct retro_subsystem_info *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        g_seen_variables = (const struct retro_variable *)data;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = PSEMU_TESTDATA_DIR;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(const enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = (struct retro_variable *)data;
        var->value = NULL;
        if (strcmp(var->key, OPTION_LINK_LAYOUT) == 0) {
            var->value = g_option_layout;
        } else if (strcmp(var->key, OPTION_LINK_UNIT_B_ID) == 0) {
            var->value = g_option_unit_b_id;
        }
        return var->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = g_options_changed;
        g_options_changed = false;
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        g_seen_geometry = *(const struct retro_game_geometry *)data;
        g_geometry_calls++;
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        g_last_message = ((const struct retro_message *)data)->msg;
        g_message_count++;
        return true;
    default:
        return false;
    }
}

static void mock_video(const void *data, unsigned width, unsigned height, size_t pitch) {
    g_video_data = (const uint32_t *)data;
    g_video_width = width;
    g_video_height = height;
    g_video_pitch = pitch;
}

static size_t mock_audio_batch(const int16_t *data, size_t frames) {
    (void)data;
    g_audio_frames += frames;
    return frames;
}

static void mock_input_poll(void) {
}

static int16_t mock_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)index;
    if (port >= 2 || device != RETRO_DEVICE_JOYPAD) {
        return 0;
    }
    return (int16_t)((g_pad[port] >> id) & 1u);
}

static void run_frames(int frames) {
    int i;
    for (i = 0; i < frames; i++) {
        retro_run();
    }
}

static bool load_link(const char *path_a, const char *path_b) {
    struct retro_game_info info[2];
    memset(info, 0, sizeof(info));
    info[0].path = path_a;
    info[0].data = g_card;
    info[0].size = sizeof(g_card);
    info[1].path = path_b;
    info[1].data = g_card;
    info[1].size = sizeof(g_card);
    return retro_load_game_special(GAME_TYPE_IR_LINK, info, 2);
}

static void test_registration(void) {
    const struct retro_subsystem_info *link = g_seen_subsystems;
    unsigned count = 0;
    CHECK(link != NULL);
    if (!link) {
        return;
    }
    CHECK(strcmp(link->ident, "ir_link") == 0);
    CHECK(link->id == GAME_TYPE_IR_LINK);
    CHECK(link->num_roms == 2);
    CHECK(link->roms[0].required && link->roms[1].required);
    /* RetroArch shows the description of the next card below its menu item. See g_link_roms. */
    CHECK(strncmp(link->roms[0].desc, "Step 1 of 3", 11) == 0);
    CHECK(strncmp(link->roms[1].desc, "Step 2 of 3", 11) == 0);
    CHECK(link->roms[0].num_memory == 1 && link->roms[1].num_memory == 1);
    CHECK(link->roms[0].memory[0].type == MEMORY_UNIT_A_FLASH);
    CHECK(link->roms[1].memory[0].type == MEMORY_UNIT_B_FLASH);
    /* The same extension as a usual load. See g_link_roms. */
    CHECK(strcmp(link->roms[0].memory[0].extension, "srm") == 0);
    CHECK(strcmp(link->roms[1].memory[0].extension, "srm") == 0);
    CHECK(link[1].desc == NULL);
    CHECK(g_seen_variables != NULL);
    while (g_seen_variables && g_seen_variables[count].key) {
        count++;
    }
    CHECK(count == 3);
    printf("test_registration OK\n");
}

static void test_load_memory_and_video(void) {
    struct retro_system_av_info av;
    CHECK(load_link("cards/unitA.mcd", "cards/unitB.mcd"));
    CHECK(g_unit_count == LINK_UNITS && g_units[UNIT_B] != NULL);

    CHECK(retro_get_memory_data(MEMORY_UNIT_A_FLASH) == psemu_flash_data(g_units[UNIT_A]));
    CHECK(retro_get_memory_data(MEMORY_UNIT_B_FLASH) == psemu_flash_data(g_units[UNIT_B]));
    CHECK(retro_get_memory_size(MEMORY_UNIT_A_FLASH) == PSEMU_FLASH_SIZE);
    CHECK(retro_get_memory_size(MEMORY_UNIT_B_FLASH) == PSEMU_FLASH_SIZE);
    CHECK(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) == NULL);
    CHECK(retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) == 0);

    /* The default option gives unit B a different ID with the same low 24 bits. */
    CHECK(psemu_get_hardware_id(g_units[UNIT_A]) == PSEMU_DEFAULT_HARDWARE_ID);
    CHECK(psemu_get_hardware_id(g_units[UNIT_B]) == (((uint32_t)'B' << 24) | 211u));

    retro_get_system_av_info(&av);
    CHECK(av.geometry.base_width == FRAME_MAX_WIDTH && av.geometry.base_height == PSEMU_LCD_HEIGHT);
    CHECK(av.geometry.max_width == FRAME_MAX_WIDTH && av.geometry.max_height == FRAME_MAX_HEIGHT);

    g_audio_frames = 0;
    g_message_count = 0;
    run_frames(1);
    /* The first frame of a link shows the position of each unit and the controls, one time. */
    CHECK(g_message_count == 1 && g_last_message && strstr(g_last_message, "unit A is on the left"));
    CHECK(g_last_message && strstr(g_last_message, "L selects unit A and R selects unit B"));
    CHECK(g_video_width == FRAME_MAX_WIDTH && g_video_height == PSEMU_LCD_HEIGHT);
    CHECK(g_video_pitch == FRAME_MAX_WIDTH * sizeof(uint32_t));
    CHECK(g_video_data && g_video_data[PSEMU_LCD_WIDTH] == LINK_GAP_COLOR);
    /* 8000Hz at 32 frames each second is 250 samples for each frame, with a remainder of at most a
       few samples between the two units. */
    CHECK(g_audio_frames >= 240 && g_audio_frames <= 250);

    g_option_layout = "Stacked";
    g_options_changed = true;
    g_geometry_calls = 0;
    run_frames(1);
    CHECK(g_message_count == 1);
    CHECK(g_geometry_calls == 1);
    CHECK(g_seen_geometry.base_width == PSEMU_LCD_WIDTH && g_seen_geometry.base_height == FRAME_MAX_HEIGHT);
    CHECK(g_video_width == PSEMU_LCD_WIDTH && g_video_height == FRAME_MAX_HEIGHT);
    CHECK(g_video_data && g_video_data[PSEMU_LCD_HEIGHT * PSEMU_LCD_WIDTH] == LINK_GAP_COLOR);
    g_option_layout = NULL;
    g_options_changed = true;
    run_frames(1);

    retro_unload_game();
    CHECK(g_unit_count == 1 && g_units[UNIT_B] == NULL);
    printf("test_load_memory_and_video OK\n");
}

static void test_same_save_name_is_refused(void) {
    /* Different directories, different extensions, and different case: RetroArch still makes one
       save file, because a save directory removes the directory and the extension. */
    CHECK(!load_link("one/Card.mcd", "two/card.gme"));
    CHECK(g_unit_count == 1 && g_units[UNIT_B] == NULL);
    printf("test_same_save_name_is_refused OK\n");
}

static void test_same_hardware_id_option(void) {
    g_option_unit_b_id = "Same as unit A";
    CHECK(load_link("unitA.mcd", "unitB.mcd"));
    CHECK(psemu_get_hardware_id(g_units[UNIT_B]) == psemu_get_hardware_id(g_units[UNIT_A]));
    retro_unload_game();
    g_option_unit_b_id = NULL;
    printf("test_same_hardware_id_option OK\n");
}

static uint64_t rx_queue_timestamp(const ir_edge_queue_t *q, uint32_t i) {
    return q->entries[(q->head + i) % IR_EDGE_QUEUE_CAPACITY].timestamp_cycles;
}

static int within(uint64_t value, uint64_t expected, uint64_t tolerance) {
    return value + tolerance >= expected && value <= expected + tolerance;
}

static void test_relay(void) {
    psemu_t *a, *b;
    uint64_t t_on, t_off, rise, fall, due;
    int frame, seen_high = 0, seen_low_after_high = 0;
    CHECK(load_link("unitA.mcd", "unitB.mcd"));
    a = g_units[UNIT_A];
    b = g_units[UNIT_B];
    /* The relay depends on two IR clocks that start together. See the comment on the IR link
       subsystem in libretro.c. */
    run_frames(3);
    retro_reset();
    CHECK(ir_get_clock_cycles(&a->ir) == 0 && ir_get_clock_cycles(&b->ir) == 0);
    run_frames(2);

    psemu_bus_write32(&b->bus, IRDA_MODE, RX_ACTIVE_MODE);
    psemu_bus_write32(&a->bus, IRDA_MODE, TX_ACTIVE_MODE);
    t_on = ir_get_clock_cycles(&a->ir);
    psemu_bus_write32(&a->bus, IRDA_DATA, IR_DATA_LED);
    run_frames(1);
    /* The rising edge is in the queue of unit B, and it is not due yet. */
    CHECK(b->ir.rx_queue.count == 1);
    CHECK(b->ir.rx_level == 0);

    t_off = ir_get_clock_cycles(&a->ir);
    psemu_bus_write32(&a->bus, IRDA_DATA, 0);
    run_frames(1);
    CHECK(b->ir.rx_queue.count == 2);
    CHECK(a->ir.rx_queue.count == 0); /* nothing goes back to the sender */
    if (b->ir.rx_queue.count == 2) {
        rise = rx_queue_timestamp(&b->ir.rx_queue, 0);
        fall = rx_queue_timestamp(&b->ir.rx_queue, 1);
        CHECK(within(rise, t_on + PLAYOUT_DELAY_CYCLES, CONVERSION_TOLERANCE_CYCLES));
        /* The space between the two edges stays the same, plus the stretch of the falling edge. */
        CHECK(fall - rise + CONVERSION_TOLERANCE_CYCLES >= t_off - t_on);
        CHECK(fall - rise <= t_off - t_on + TX_FALL_STRETCH_MAX_CYCLES + CONVERSION_TOLERANCE_CYCLES);
    }

    /* Unit B shows the carrier from the due time of the rising edge, and not before it. */
    due = t_on + PLAYOUT_DELAY_CYCLES;
    for (frame = 0; frame < 10; frame++) {
        uint64_t before = ir_get_clock_cycles(&b->ir);
        run_frames(1);
        if (b->ir.rx_level == 1 && !seen_high) {
            seen_high = 1;
            CHECK(before < due + CONVERSION_TOLERANCE_CYCLES);
            CHECK(ir_get_clock_cycles(&b->ir) >= due);
        } else if (b->ir.rx_level == 0 && seen_high) {
            seen_low_after_high = 1;
        }
    }
    CHECK(seen_high);
    CHECK(seen_low_after_high);
    CHECK(b->ir.mode == RX_ACTIVE_MODE); /* the BIOS did not change the mode during the test */
    retro_unload_game();
    printf("test_relay OK\n");
}

static void run_scripted_frames(void) {
    int i;
    for (i = 0; i < 8; i++) {
        g_pad[0] = (i >= 2 && i < 4) ? (1u << RETRO_DEVICE_ID_JOYPAD_A) : 0u;
        g_pad[1] = (i >= 5 && i < 7) ? (1u << RETRO_DEVICE_ID_JOYPAD_DOWN) : 0u;
        retro_run();
    }
    g_pad[0] = g_pad[1] = 0;
}

static void test_state_is_deterministic(void) {
    size_t size;
    uint8_t *start, *first, *second;
    psemu_t *a;
    CHECK(load_link("unitA.mcd", "unitB.mcd"));
    a = g_units[UNIT_A];
    run_frames(2);
    psemu_bus_write32(&g_units[UNIT_B]->bus, IRDA_MODE, RX_ACTIVE_MODE);
    psemu_bus_write32(&a->bus, IRDA_MODE, TX_ACTIVE_MODE);
    psemu_bus_write32(&a->bus, IRDA_DATA, IR_DATA_LED);
    run_frames(1);
    psemu_bus_write32(&a->bus, IRDA_DATA, 0);

    size = retro_serialize_size();
    CHECK(size == LINK_STATE_HEADER_SIZE + psemu_state_size(a) + psemu_state_size(g_units[UNIT_B]));
    start = (uint8_t *)malloc(size);
    first = (uint8_t *)malloc(size);
    second = (uint8_t *)malloc(size);
    CHECK(start && first && second);
    if (!start || !first || !second) {
        return;
    }
    /* The state holds an edge in the TX queue of unit A, and an edge in transit to unit B. */
    CHECK(retro_serialize(start, size));
    run_scripted_frames();
    CHECK(retro_serialize(first, size));
    CHECK(retro_unserialize(start, size));
    run_scripted_frames();
    CHECK(retro_serialize(second, size));
    CHECK(memcmp(first, second, size) == 0);

    CHECK(!retro_unserialize(start, size - 1));
    start[0] ^= 0xFFu;
    CHECK(!retro_unserialize(start, size));

    free(start);
    free(first);
    free(second);
    retro_unload_game();
    printf("test_state_is_deterministic OK\n");
}

static void test_controller_selection(void) {
    CHECK(load_link("unitA.mcd", "unitB.mcd"));
    g_pad[0] = 1u << RETRO_DEVICE_ID_JOYPAD_A;
    run_frames(1);
    CHECK(g_units[UNIT_A]->buttons == PSEMU_BUTTON_FIRE && g_units[UNIT_B]->buttons == 0);

    g_pad[0] = 1u << RETRO_DEVICE_ID_JOYPAD_R;
    run_frames(1);
    CHECK(g_port0_unit == UNIT_B);
    g_pad[0] = 1u << RETRO_DEVICE_ID_JOYPAD_A;
    g_pad[1] = 1u << RETRO_DEVICE_ID_JOYPAD_UP;
    run_frames(1);
    CHECK(g_units[UNIT_A]->buttons == 0);
    CHECK(g_units[UNIT_B]->buttons == (PSEMU_BUTTON_FIRE | PSEMU_BUTTON_UP));

    g_pad[0] = 1u << RETRO_DEVICE_ID_JOYPAD_L;
    g_pad[1] = 0;
    run_frames(1);
    CHECK(g_port0_unit == UNIT_A);
    g_pad[0] = 0;
    retro_unload_game();
    printf("test_controller_selection OK\n");
}

static void test_usual_load_after_link(void) {
    struct retro_game_info info;
    struct retro_system_av_info av;
    memset(&info, 0, sizeof(info));
    info.path = "unitA.mcd";
    info.data = g_card;
    info.size = sizeof(g_card);
    CHECK(retro_load_game(&info));
    CHECK(g_unit_count == 1);
    retro_get_system_av_info(&av);
    CHECK(av.geometry.base_width == PSEMU_LCD_WIDTH && av.geometry.base_height == PSEMU_LCD_HEIGHT);
    CHECK(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) == psemu_flash_data(g_units[UNIT_A]));
    CHECK(retro_get_memory_data(MEMORY_UNIT_A_FLASH) == NULL);
    CHECK(retro_serialize_size() == psemu_state_size(g_units[UNIT_A]));
    g_message_count = 0;
    run_frames(1);
    CHECK(g_message_count == 0); /* a usual load shows no link message */
    CHECK(g_video_width == PSEMU_LCD_WIDTH && g_video_height == PSEMU_LCD_HEIGHT);
    CHECK(g_video_pitch == PSEMU_LCD_WIDTH * sizeof(uint32_t));
    retro_unload_game();
    printf("test_usual_load_after_link OK\n");
}

int main(void) {
    FILE *f = fopen(PSEMU_TESTDATA_DIR "/pocketstation.bin", "rb");
    if (!f) {
        printf("SKIP: no BIOS dump at %s/pocketstation.bin\n", PSEMU_TESTDATA_DIR);
        return SKIP_EXIT_CODE;
    }
    fclose(f);

    retro_set_environment(mock_environment);
    retro_set_video_refresh(mock_video);
    retro_set_audio_sample_batch(mock_audio_batch);
    retro_set_input_poll(mock_input_poll);
    retro_set_input_state(mock_input_state);
    retro_init();

    test_registration();
    test_load_memory_and_video();
    test_same_save_name_is_refused();
    test_same_hardware_id_option();
    test_relay();
    test_state_is_deterministic();
    test_controller_selection();
    test_usual_load_after_link();

    retro_deinit();
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("All libretro link tests passed\n");
    return 0;
}
