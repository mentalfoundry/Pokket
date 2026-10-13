/* Verification for movie.c and rewind.c, the rerecording base of the desktop frontend.

   A movie that does not play back exactly is worse than no movie: the desync shows only after the
   user has spent hours on it. Thus this test checks the properties that a person cannot see quickly:
   the file round trip, the mode changes of read-only and read-write, the timeline checks of a state
   load, the exact reconstruction of each rewind state, and that a playback gives the same machine,
   byte for byte, as the recording.

   The last check needs a BIOS dump, because without a BIOS psemu_run does nothing. It skips without a
   dump, with a message, and the other checks still run. Set PSEMU_TEST_BIOS to use a different
   dump. */

/* Its checks are its whole purpose, so they have to survive NDEBUG. See tests/cpu_test.c. */
#undef NDEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "movie.h"
#include "rewind.h"

/* The budget of one frame of the main loop of main.c. */
#define FRAME_CYCLES 33000u

static int failures;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            printf("FAIL: %s\n", (msg));                                                                     \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static char temp_dir[MAX_PATH];

static void temp_path(char *out, size_t size, const char *name) {
    snprintf(out, size, "%s%s", temp_dir, name);
}

static void test_crc32(void) {
    static const uint8_t check[] = "123456789";
    CHECK(movie_crc32(check, 9) == 0xCBF43926u, "CRC-32 of \"123456789\" must be the standard check value");
}

/* Records `count` frames with the input i * 3. */
static void record_frames(movie_t *m, uint32_t count) {
    uint32_t i;
    for (i = 0; i < count; i++) {
        uint8_t live = (uint8_t)((m->frame * 3u) & MOVIE_INPUT_BUTTONS);
        CHECK(movie_next_input(m, live) == live, "a recording must give the live input back");
    }
}

static void test_file_round_trip(void) {
    char path[MAX_PATH];
    psemu_t *ps = psemu_create();
    movie_t m, r;
    uint32_t i;
    char err[128];

    temp_path(path, sizeof(path), "movie_selftest_round_trip.pkm");
    movie_init(&m);
    movie_init(&r);
    CHECK(movie_start_recording(&m, ps, path, 0x0123456789ABCDEFull, MOVIE_FLAG_POWER_ON, 0x11111111u, 1234u,
              0x22222222u, "v9.99"),
        "a recording must start");
    record_frames(&m, 100);
    CHECK(m.mode == MOVIE_RECORDING && m.length == 100 && m.frame == 100, "100 recorded frames");
    m.rerecords = 7;
    CHECK(movie_write(&m), "the movie must write");
    CHECK(!m.dirty, "a write must clear dirty");

    CHECK(movie_open(&r, path, err, sizeof(err)), "the movie must open");
    CHECK(r.mode == MOVIE_PLAYING && r.read_only && r.frame == 0, "an opened movie plays read-only from frame 0");
    CHECK(r.id == m.id && r.flags == MOVIE_FLAG_POWER_ON && r.rerecords == 7 && r.bios_crc == m.bios_crc &&
              r.content_size == 1234u && r.content_hash == m.content_hash && strcmp(r.emulator_version, "v9.99") == 0,
        "each header field must round-trip");
    CHECK(r.start_state_size == m.start_state_size && memcmp(r.start_state, m.start_state, m.start_state_size) == 0,
        "the start state must round-trip");
    CHECK(r.length == 100 && memcmp(r.inputs, m.inputs, 100) == 0, "the inputs must round-trip");

    for (i = 0; i < 100; i++) {
        CHECK(movie_next_input(&r, 0x1Fu) == m.inputs[i], "a playback must give the movie input, not the live input");
    }
    CHECK(r.mode == MOVIE_FINISHED && r.frame == 100, "a read-only playback stops at its end");
    CHECK(movie_next_input(&r, 0x05u) == 0x05u && r.frame == 101, "a finished movie gives the live input");

    movie_close(&r);
    movie_close(&m);
    DeleteFileA(path);

    /* A truncated file must give an error, not a short movie. */
    {
        FILE *f;
        uint8_t junk[200];
        memset(junk, 0, sizeof(junk));
        memcpy(junk, "PKMV", 4);
        junk[4] = 1;
        f = fopen(path, "wb");
        fwrite(junk, 1, sizeof(junk), f);
        fclose(f);
        CHECK(!movie_open(&r, path, err, sizeof(err)), "a damaged movie must not open");
        DeleteFileA(path);
    }
    psemu_destroy(ps);
}

static void test_read_only_and_read_write(void) {
    psemu_t *ps = psemu_create();
    movie_t m;
    movie_init(&m);
    movie_start_recording(&m, ps, "unused.pkm", 1u, 0u, 0u, 0u, 0u, "");
    record_frames(&m, 10);

    movie_set_read_only(&m, 1);
    CHECK(m.mode == MOVIE_FINISHED, "read-only during a recording stops the recording at its end");
    movie_set_read_only(&m, 0);
    CHECK(m.mode == MOVIE_RECORDING, "read-write at the end of a movie continues the recording");

    /* A read-write playback that gets to the end continues as a recording. */
    m.mode = MOVIE_PLAYING;
    m.frame = 8;
    movie_next_input(&m, 0);
    CHECK(m.mode == MOVIE_PLAYING, "a playback before the end stays a playback");
    movie_next_input(&m, 0);
    CHECK(m.mode == MOVIE_RECORDING && m.frame == 10, "a read-write playback continues as a recording at its end");
    movie_next_input(&m, 0x02u);
    CHECK(m.length == 11 && m.inputs[10] == 0x02u, "the continued recording appends");

    /* A rewind in read-write mode removes the later inputs and counts one rerecord. */
    movie_seek(&m, 4, 1);
    CHECK(m.mode == MOVIE_RECORDING && m.length == 4 && m.frame == 4 && m.rerecords == 1,
        "a read-write rewind truncates and counts a rerecord");

    /* A rewind in read-only mode only moves the position. */
    record_frames(&m, 6);
    movie_set_read_only(&m, 1);
    movie_seek(&m, 3, 1);
    CHECK(m.mode == MOVIE_PLAYING && m.length == 10 && m.frame == 3 && m.rerecords == 1,
        "a read-only rewind keeps the movie");

    movie_close(&m);
    psemu_destroy(ps);
}

static void test_trailers(void) {
    psemu_t *ps = psemu_create();
    movie_t m, other;
    uint8_t t5[64], t12[64], branch[64], foreign[64];
    size_t t5_size, t12_size, branch_size, foreign_size;

    movie_init(&m);
    movie_init(&other);
    movie_start_recording(&m, ps, "unused.pkm", 42u, 0u, 0u, 0u, 0u, "");
    record_frames(&m, 5);
    t5_size = movie_trailer_size(&m);
    movie_write_trailer(&m, t5);
    CHECK(t5_size == 20u + 5u && movie_trailer_is_valid(t5, t5_size), "a trailer holds the inputs up to its frame");
    record_frames(&m, 5);

    /* A different branch of the same movie: frame 6 differs. */
    {
        movie_t b;
        movie_init(&b);
        movie_start_recording(&b, ps, "unused.pkm", 42u, 0u, 0u, 0u, 0u, "");
        record_frames(&b, 5);
        movie_next_input(&b, 0x10u);
        movie_next_input(&b, 0x10u);
        branch_size = movie_trailer_size(&b);
        movie_write_trailer(&b, branch);
        movie_close(&b);
    }
    movie_start_recording(&other, ps, "unused.pkm", 43u, 0u, 0u, 0u, 0u, "");
    record_frames(&other, 3);
    foreign_size = movie_trailer_size(&other);
    movie_write_trailer(&other, foreign);

    CHECK(movie_check_trailer(&m, foreign, foreign_size) == MOVIE_TRAILER_OTHER_MOVIE,
        "a state of a different movie is refused");
    CHECK(movie_check_trailer(&m, t5, t5_size - 1) == MOVIE_TRAILER_BAD, "a short trailer is refused");

    /* Read-only. */
    movie_set_read_only(&m, 1);
    CHECK(movie_check_trailer(&m, t5, t5_size) == MOVIE_TRAILER_OK, "a read-only load inside the timeline is accepted");
    CHECK(movie_check_trailer(&m, branch, branch_size) == MOVIE_TRAILER_NOT_IN_TIMELINE,
        "a read-only load from a different branch is refused");
    movie_apply_trailer(&m, t5, t5_size);
    CHECK(m.mode == MOVIE_PLAYING && m.frame == 5 && m.length == 10 && m.rerecords == 0,
        "a read-only load moves the position only");

    /* A state after the end of a read-only movie. */
    m.frame = 10;
    movie_set_read_only(&m, 1);
    movie_next_input(&m, 0);
    movie_next_input(&m, 0);
    t12_size = movie_trailer_size(&m);
    movie_write_trailer(&m, t12);
    CHECK(m.mode == MOVIE_FINISHED && t12_size == 20u + 10u, "a state after the end holds the full movie");
    CHECK(movie_check_trailer(&m, t12, t12_size) == MOVIE_TRAILER_OK, "read-only accepts a state after the end");

    /* Read-write. */
    movie_set_read_only(&m, 0);
    CHECK(movie_check_trailer(&m, t12, t12_size) == MOVIE_TRAILER_PAST_END,
        "read-write refuses a state after the end, which has no inputs to record");
    CHECK(movie_check_trailer(&m, branch, branch_size) == MOVIE_TRAILER_OK,
        "read-write accepts a different branch of the same movie");
    CHECK(movie_apply_trailer(&m, branch, branch_size), "the branch must apply");
    CHECK(m.mode == MOVIE_RECORDING && m.frame == 7 && m.length == 7 && m.inputs[5] == 0x10u && m.rerecords == 1,
        "a read-write load takes the inputs of the state and counts a rerecord");

    movie_close(&other);
    movie_close(&m);
    psemu_destroy(ps);
}

/* A state of `size` bytes that changes a little at each step, as a machine does. */
static void mutate(uint8_t *state, size_t size, uint32_t step) {
    uint32_t i;
    for (i = 0; i < 40; i++) {
        state[(step * 7919u + i * 104729u) % size] ^= (uint8_t)(step + i + 1u);
    }
    /* A long change also, as a flash write gives. */
    if (step % 17u == 0) {
        memset(state + (step * 31u) % (size - 600u), (int)step, 600);
    }
}

static void test_rewind(void) {
    const size_t size = 50000;
    const uint32_t steps = 120;
    uint8_t *history = (uint8_t *)malloc(size * steps);
    uint8_t *out = (uint8_t *)malloc(size);
    rewind_t r;
    uint32_t i, frame;
    int exact = 1;

    for (i = 0; i < size; i++) {
        history[i] = (uint8_t)(i * 13u);
    }
    for (i = 1; i < steps; i++) {
        memcpy(history + i * size, history + (i - 1) * size, size);
        mutate(history + i * size, size, i);
    }

    CHECK(rewind_init(&r, size, 1000, 64u * 1024u * 1024u), "rewind_init");
    for (i = 0; i < steps; i++) {
        rewind_push(&r, history + i * size, 1000u + i);
    }
    CHECK(r.bytes < size * 4u, "the differences must be much smaller than the states");
    for (i = steps; i-- > 0;) {
        if (!rewind_pop(&r, out, &frame) || frame != 1000u + i || memcmp(out, history + i * size, size) != 0) {
            exact = 0;
        }
    }
    CHECK(exact, "each rewind step must give the exact state and frame, newest first");
    CHECK(!rewind_pop(&r, out, &frame), "an empty buffer gives nothing");

    /* A limit of 10 earlier frames keeps the newest 11 states. */
    rewind_free(&r);
    rewind_init(&r, size, 10, 64u * 1024u * 1024u);
    for (i = 0; i < steps; i++) {
        rewind_push(&r, history + i * size, i);
    }
    exact = 1;
    for (i = steps; i-- > steps - 11u;) {
        if (!rewind_pop(&r, out, &frame) || frame != i || memcmp(out, history + i * size, size) != 0) {
            exact = 0;
        }
    }
    CHECK(exact && !rewind_pop(&r, out, &frame), "the frame limit drops the oldest states");

    /* A clear empties the buffer. */
    rewind_push(&r, history, 0);
    rewind_clear(&r);
    CHECK(!rewind_pop(&r, out, &frame), "a clear empties the buffer");

    rewind_free(&r);
    free(out);
    free(history);
}

/* The input script of the BIOS test: presses of two frames, as the frontend gives, at intervals. */
static uint8_t script_input(uint32_t frame, uint32_t seed) {
    static const uint8_t buttons[] = {PSEMU_BUTTON_DOWN, PSEMU_BUTTON_FIRE, PSEMU_BUTTON_RIGHT, PSEMU_BUTTON_FIRE,
        PSEMU_BUTTON_UP, PSEMU_BUTTON_LEFT};
    uint32_t slot = frame / 8u;
    if (frame % 8u >= 2u) {
        return 0;
    }
    return buttons[(slot * 5u + seed) % (sizeof(buttons) / sizeof(buttons[0]))];
}

static void run_frame(movie_t *m, psemu_t *ps, uint8_t live) {
    uint8_t input = movie_next_input(m, live);
    if (input & MOVIE_INPUT_RESET) {
        psemu_reset(ps);
    }
    psemu_set_buttons(ps, input & MOVIE_INPUT_BUTTONS);
    psemu_run(ps, FRAME_CYCLES);
}

static uint8_t *capture(const psemu_t *ps) {
    size_t size = psemu_state_size(ps);
    uint8_t *buf = (uint8_t *)malloc(size);
    psemu_save_state(ps, buf, size);
    return buf;
}

static psemu_t *create_with_bios(const uint8_t *bios, size_t size) {
    psemu_t *ps = psemu_create();
    psemu_load_bios(ps, bios, size);
    psemu_reset(ps);
    return ps;
}

static void test_playback_is_exact(void) {
    const char *env = getenv("PSEMU_TEST_BIOS");
    const char *bios_path = (env && env[0]) ? env : PSEMU_TESTDATA_DIR "/J110.bin";
    uint8_t bios[PSEMU_BIOS_SIZE];
    char path[MAX_PATH], err[128];
    FILE *f = fopen(bios_path, "rb");
    psemu_t *ps1, *ps2, *ps3;
    movie_t m, m2, m3;
    rewind_t r;
    uint8_t *s200 = NULL, *final_a, *final_b, *final_c, *final_c2, *popped;
    uint8_t t200[1024], t250[1024];
    size_t t200_size = 0, t250_size = 0, state_size;
    uint32_t i, frame;

    if (!f || fread(bios, 1, sizeof(bios), f) != sizeof(bios)) {
        printf("SKIP: playback determinism (no BIOS dump at %s)\n", bios_path);
        if (f) {
            fclose(f);
        }
        return;
    }
    fclose(f);
    temp_path(path, sizeof(path), "movie_selftest_bios.pkm");

    /* Record 400 frames from power-on, with a reset at frame 150. Push each state into the rewind
       buffer, as the frontend does. */
    ps1 = create_with_bios(bios, sizeof(bios));
    state_size = psemu_state_size(ps1);
    popped = (uint8_t *)malloc(state_size);
    movie_init(&m);
    movie_start_recording(&m, ps1, path, 0xFEEDu, MOVIE_FLAG_POWER_ON, movie_crc32(bios, sizeof(bios)), 0, 0, "test");
    rewind_init(&r, state_size, 10000, 256u * 1024u * 1024u);
    for (i = 0; i < 400; i++) {
        uint8_t *now = capture(ps1);
        rewind_push(&r, now, m.frame);
        if (i == 200) {
            s200 = now;
            t200_size = movie_trailer_size(&m);
            movie_write_trailer(&m, t200);
        } else {
            free(now);
        }
        if (i == 250) {
            t250_size = movie_trailer_size(&m);
            movie_write_trailer(&m, t250);
        }
        run_frame(&m, ps1, (uint8_t)(script_input(i, 0) | (i == 150 ? MOVIE_INPUT_RESET : 0)));
    }
    final_a = capture(ps1);
    CHECK(memcmp(final_a, m.start_state, state_size) != 0, "400 frames must change the machine");
    CHECK(m.inputs[150] & MOVIE_INPUT_RESET, "the movie must hold the reset");

    /* Each rewind step back to frame 200 must give the recorded state exactly. */
    {
        int popped_ok;
        do {
            popped_ok = rewind_pop(&r, popped, &frame);
        } while (popped_ok && frame > 200);
        CHECK(popped_ok && frame == 200 && memcmp(popped, s200, state_size) == 0,
            "a rewind to frame 200 gives the state of frame 200");
    }
    rewind_free(&r);

    CHECK(movie_write(&m), "the movie must write");

    /* A playback in a new machine gives the same machine. */
    movie_init(&m2);
    CHECK(movie_open(&m2, path, err, sizeof(err)), "the movie must open");
    ps2 = create_with_bios(bios, sizeof(bios));
    CHECK(movie_load_start_state(&m2, ps2) == PSEMU_OK, "the start state must load");
    while (m2.mode == MOVIE_PLAYING) {
        run_frame(&m2, ps2, 0x1Fu);
    }
    final_b = capture(ps2);
    CHECK(m2.frame == 400 && memcmp(final_a, final_b, state_size) == 0,
        "a playback must give the recorded machine, byte for byte");

    /* Rerecord: load the state of frame 200 in read-write mode, and record a different ending. */
    movie_set_read_only(&m2, 0);
    CHECK(movie_check_trailer(&m2, t200, t200_size) == MOVIE_TRAILER_OK, "the frame-200 state belongs to the movie");
    CHECK(psemu_load_state(ps2, s200, state_size) == PSEMU_OK, "the frame-200 state must load");
    movie_apply_trailer(&m2, t200, t200_size);
    CHECK(m2.mode == MOVIE_RECORDING && m2.frame == 200 && m2.length == 200 && m2.rerecords == 1,
        "a read-write load continues the recording from frame 200");
    for (i = 200; i < 300; i++) {
        run_frame(&m2, ps2, script_input(i, 3));
    }
    final_c = capture(ps2);
    CHECK(movie_write(&m2), "the rerecorded movie must write");

    movie_init(&m3);
    CHECK(movie_open(&m3, path, err, sizeof(err)), "the rerecorded movie must open");
    CHECK(m3.length == 300 && m3.rerecords == 1, "the rerecorded movie holds the new ending and the count");
    CHECK(movie_check_trailer(&m3, t200, t200_size) == MOVIE_TRAILER_OK,
        "read-only: the frame-200 state is still inside the timeline");
    CHECK(movie_check_trailer(&m3, t250, t250_size) == MOVIE_TRAILER_NOT_IN_TIMELINE,
        "read-only: a frame-250 state of the old ending is not");
    ps3 = create_with_bios(bios, sizeof(bios));
    movie_load_start_state(&m3, ps3);
    while (m3.mode == MOVIE_PLAYING) {
        run_frame(&m3, ps3, 0);
    }
    final_c2 = capture(ps3);
    CHECK(memcmp(final_c, final_c2, state_size) == 0, "a playback of the rerecorded movie must give its machine");

    movie_close(&m);
    movie_close(&m2);
    movie_close(&m3);
    psemu_destroy(ps1);
    psemu_destroy(ps2);
    psemu_destroy(ps3);
    free(s200);
    free(final_a);
    free(final_b);
    free(final_c);
    free(final_c2);
    free(popped);
    DeleteFileA(path);
    printf("playback determinism checked against %s\n", bios_path);
}

int main(void) {
    GetTempPathA(sizeof(temp_dir), temp_dir);

    test_crc32();
    test_file_round_trip();
    test_read_only_and_read_write();
    test_trailers();
    test_rewind();
    test_playback_is_exact();

    if (failures) {
        printf("movie_selftest: %d failure(s)\n", failures);
        return 1;
    }
    printf("movie_selftest: all checks passed\n");
    return 0;
}
