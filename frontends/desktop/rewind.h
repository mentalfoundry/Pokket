#ifndef POKKETSTATION_REWIND_H
#define POKKETSTATION_REWIND_H

#include <stddef.h>
#include <stdint.h>

/* A history of machine states, one for each frame, for the rewind function of the desktop frontend.

   A state from psemu_save_state is approximately 219KB, and most of it is flash. One frame changes
   few bytes of it: usually some RAM, the CPU registers, and the timers. Thus this buffer keeps the
   newest state in full, and for each earlier frame only the difference to the next frame. A
   difference is the XOR of the two states, with each run of zero bytes removed. Thus one minute of
   history usually costs some megabytes, and not 420MB.

   The states are from psemu_save_state, and not from psemu_snapshot_save. A snapshot is a raw copy
   of psemu_t, and the trace ring of the CPU in it changes completely at each frame. The size of the
   state is the same for each state of the machine (see "Save states" in docs/hardware-notes.md).
   That is a requirement here: an XOR of two states must align field by field.

   Each entry holds a frame number from the caller. The desktop frontend gives the position of the
   active movie, thus a rewind step can move the movie to the same frame. */

typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t frame;
} rewind_entry_t;

typedef struct {
    size_t state_size;
    uint8_t *newest;
    uint32_t newest_frame;
    int has_newest;
    uint8_t *scratch;
    rewind_entry_t *entries; /* a ring, oldest first from `head` */
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
    size_t bytes;
    size_t byte_budget;
} rewind_t;

/* `max_frames` limits the number of earlier frames, and `byte_budget` limits the memory of their
   differences. The oldest frame goes first when either limit is reached. Returns 0 if no memory is
   available. */
int rewind_init(rewind_t *r, size_t state_size, uint32_t max_frames, size_t byte_budget);
void rewind_free(rewind_t *r);

/* Removes each state. Call this function when the history stops being the past of the machine: after
   a state load, or after a load of a different BIOS or content. */
void rewind_clear(rewind_t *r);

/* Adds `state`, which is state_size bytes, as the newest state. Call this function before the run of
   frame `frame`. */
void rewind_push(rewind_t *r, const uint8_t *state, uint32_t frame);

/* Removes the newest state and copies it to `out`. Thus the first call gives the state before the run
   of the last frame. Returns 0 if the buffer is empty. */
int rewind_pop(rewind_t *r, uint8_t *out, uint32_t *out_frame);

#endif
