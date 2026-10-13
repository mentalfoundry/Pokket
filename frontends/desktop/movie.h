#ifndef POKKETSTATION_MOVIE_H
#define POKKETSTATION_MOVIE_H

#include <stddef.h>
#include <stdint.h>

#include "psemu/psemu.h"

/* An input movie: a start state, then one input byte for each frame. This is the base of
   rerecording. A run from the start state with the same bytes gives the same machine, because the
   core is deterministic (see psemu_set_datetime and "Save states" in docs/hardware-notes.md).

   ONE FRAME IS ONE ITERATION OF THE MAIN LOOP OF THE DESKTOP FRONTEND: one psemu_set_buttons call,
   then one psemu_run call. The input byte of frame N is the value that the frontend gives to the
   machine before the run of frame N. A movie holds the buttons after the minimum-press filter of the
   frontend (see BUTTON_MIN_PRESS_FRAMES in main.c). Thus a playback does not need that filter.

   THE MOVIE HOLDS ITS OWN START STATE. A movie from power-on holds the state immediately after
   psemu_reset. That state holds flash, thus the card and the hardware ID. A playback does not read
   the card file on disk, and save write-back cannot make a movie desync. The state holds no BIOS. The
   header holds a CRC-32 of the BIOS, and the frontend compares it before a playback.

   A movie has a read-only mode and a read-write mode:
   - Read-only: a playback gives the inputs of the movie, and a state load moves the position inside
     the movie. The movie does not change.
   - Read-write: a state load replaces the inputs after the state with nothing, and recording
     continues from that frame. Each such load adds one to the rerecord count. A playback that gets
     to the end of the movie continues as a recording.

   This module has no SDL code and no Win32 code except the file replacement, thus
   movie_selftest.c can test it without a window. */

/* The five psemu_button bits. */
#define MOVIE_INPUT_BUTTONS 0x1Fu
/* A reset before the run of this frame. A reset is an input, thus a movie must hold it. */
#define MOVIE_INPUT_RESET 0x80u

/* Bit 0 of the flags field: the start state is the state immediately after psemu_reset. If this bit
   is clear, the movie starts from a state that the user had at that time. */
#define MOVIE_FLAG_POWER_ON 0x1u

#define MOVIE_EMULATOR_VERSION_SIZE 32u

typedef enum {
    MOVIE_INACTIVE = 0,
    /* Each frame appends the live input. The position is always the end of the movie. */
    MOVIE_RECORDING,
    /* Each frame gives the next input of the movie. The live input has no effect. */
    MOVIE_PLAYING,
    /* A read-only movie after its last frame. The live input operates the machine again, and the
       movie does not record it. The position continues to count. */
    MOVIE_FINISHED
} movie_mode_t;

typedef struct {
    movie_mode_t mode;
    int read_only;
    /* The inputs or the rerecord count changed after the last movie_write. */
    int dirty;
    char path[1024];
    /* A random value that identifies one movie. A save state made during the movie holds it, and a
       load into a different movie fails. */
    uint64_t id;
    uint32_t flags;
    uint32_t rerecords;
    uint32_t bios_crc;
    /* The identity of the loaded content (see psemu_content_identity_hash). The frontend refuses a
       playback with a different app or card, for the same reason that it refuses such a state load. */
    uint32_t content_size;
    uint32_t content_hash;
    char emulator_version[MOVIE_EMULATOR_VERSION_SIZE];
    uint8_t *start_state;
    size_t start_state_size;
    uint8_t *inputs;
    uint32_t length;
    uint32_t capacity;
    /* The number of frames since the start state. While the mode is MOVIE_PLAYING, this value is less
       than `length`. While the mode is MOVIE_RECORDING, it is equal to `length`. */
    uint32_t frame;
} movie_t;

void movie_init(movie_t *m);

/* Releases the memory of the movie and sets the mode to MOVIE_INACTIVE. This function writes no file.
   Call movie_write first if `dirty` is set. */
void movie_close(movie_t *m);

static inline int movie_is_active(const movie_t *m) {
    return m->mode != MOVIE_INACTIVE;
}

/* Starts a new recording at the current state of `ps`. The caller resets the machine first for a
   movie from power-on, and gives MOVIE_FLAG_POWER_ON in `flags`. Returns 0 if no memory is
   available. */
int movie_start_recording(movie_t *m, const psemu_t *ps, const char *path, uint64_t id, uint32_t flags,
    uint32_t bios_crc, uint32_t content_size, uint32_t content_hash, const char *emulator_version);

/* Reads a movie file. The mode becomes MOVIE_PLAYING and read-only, at frame 0. This function does
   not change the machine: the caller compares the BIOS and the content, and then calls
   movie_load_start_state. Returns 0 and writes a message into `err` if the file is not a movie. */
int movie_open(movie_t *m, const char *path, char *err, size_t err_size);

psemu_status movie_load_start_state(const movie_t *m, psemu_t *ps);

/* Writes the movie to `path` through a temporary file. Thus a failed write does not damage an older
   copy. Clears `dirty` on success. Returns 0 on failure. */
int movie_write(movie_t *m);

/* Gives the input for the next frame and moves the position by one frame. `live` is the input of the
   user, with MOVIE_INPUT_RESET if the user asked for a reset. The caller applies the return value:
   a reset if MOVIE_INPUT_RESET is set, then the buttons. */
uint8_t movie_next_input(movie_t *m, uint8_t live);

/* Read-only during a recording stops the recording: the movie becomes finished at its end.
   Read-write at the end of a playback continues the movie as a recording. */
void movie_set_read_only(movie_t *m, int read_only);

/* Moves the position to `frame` after a rewind step loaded an earlier state. In read-write mode, the
   inputs after `frame` are removed, and recording continues. `count_rerecord` adds one to the
   rerecord count for that removal: give it for the first step of one rewind only. */
void movie_seek(movie_t *m, uint32_t frame, int count_rerecord);

/* The movie trailer of a save state. A save state made during a movie holds the id of the movie, the
   position, and the inputs up to that position. The inputs are necessary: a read-write load
   replaces the inputs of the movie with them, and a read-only load compares them with the movie. */
size_t movie_trailer_size(const movie_t *m);
void movie_write_trailer(const movie_t *m, uint8_t *buf);

typedef enum {
    MOVIE_TRAILER_OK = 0,
    /* The bytes are not a movie trailer. */
    MOVIE_TRAILER_BAD,
    /* The state is from a different movie. */
    MOVIE_TRAILER_OTHER_MOVIE,
    /* Read-only: the inputs of the state are different from the inputs of the movie. A load then
       shows a machine that the movie never makes. */
    MOVIE_TRAILER_NOT_IN_TIMELINE,
    /* Read-write: the state is after the end of the movie, where the movie holds no input. */
    MOVIE_TRAILER_PAST_END
} movie_trailer_result_t;

/* Tests only the structure of a trailer. A state load while no movie is active uses this test: the
   trailer then has no function, but bytes that are not a trailer show a damaged file. */
int movie_trailer_is_valid(const uint8_t *buf, size_t size);

/* Tests a trailer against the movie and the current mode. Call this function before
   psemu_load_state, thus a refused load does not change the machine. */
movie_trailer_result_t movie_check_trailer(const movie_t *m, const uint8_t *buf, size_t size);

/* Moves the movie to the state of the trailer. Call this function only after movie_check_trailer
   gives MOVIE_TRAILER_OK and psemu_load_state succeeds. Returns 0 if no memory is available. */
int movie_apply_trailer(movie_t *m, const uint8_t *buf, size_t size);

/* The CRC-32 of IEEE 802.3, the same value that zip and PNG use. */
uint32_t movie_crc32(const uint8_t *data, size_t size);

#endif
