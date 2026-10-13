#include "rewind.h"

#include <stdlib.h>
#include <string.h>

/* The format of one difference: a list of records, each record a u32 count of equal bytes to skip, a
   u32 count of different bytes, and then the XOR of those different bytes. The integers use the byte
   order of the host, because a difference never leaves this process.

   A run of fewer than RUN_MIN equal bytes stays inside a record. A new record costs 8 bytes, thus a
   shorter run costs more as a record than as XOR data. This limit also gives the maximum size of a
   difference: each record holds at least one byte and is followed by at least RUN_MIN equal bytes,
   thus the full difference is less than two times the size of a state. */
#define RUN_MIN 8u

static void put_u32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, sizeof(v));
}

static uint32_t get_u32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static size_t delta_encode(const uint8_t *a, const uint8_t *b, size_t n, uint8_t *out) {
    size_t i = 0;
    size_t o = 0;
    while (i < n) {
        size_t skip_start = i;
        size_t lit_start, lit_end, j, k;
        while (i < n && a[i] == b[i]) {
            i++;
        }
        if (i == n) {
            break;
        }
        lit_start = i;
        lit_end = i + 1;
        for (j = lit_end; j < n && j - lit_end < RUN_MIN; j++) {
            if (a[j] != b[j]) {
                lit_end = j + 1;
            }
        }
        put_u32(out + o, (uint32_t)(lit_start - skip_start));
        put_u32(out + o + 4, (uint32_t)(lit_end - lit_start));
        o += 8;
        for (k = lit_start; k < lit_end; k++) {
            out[o++] = a[k] ^ b[k];
        }
        i = lit_end;
    }
    return o;
}

static void delta_apply(uint8_t *buf, const uint8_t *d, size_t size) {
    size_t o = 0;
    size_t pos = 0;
    while (o < size) {
        uint32_t skip = get_u32(d + o);
        uint32_t len = get_u32(d + o + 4);
        uint32_t k;
        o += 8;
        pos += skip;
        for (k = 0; k < len; k++) {
            buf[pos + k] ^= d[o + k];
        }
        pos += len;
        o += len;
    }
}

int rewind_init(rewind_t *r, size_t state_size, uint32_t max_frames, size_t byte_budget) {
    memset(r, 0, sizeof(*r));
    r->state_size = state_size;
    r->capacity = max_frames;
    r->byte_budget = byte_budget;
    r->newest = (uint8_t *)malloc(state_size);
    r->scratch = (uint8_t *)malloc(state_size * 2u + 16u);
    r->entries = (rewind_entry_t *)calloc(max_frames, sizeof(rewind_entry_t));
    if (!r->newest || !r->scratch || !r->entries) {
        rewind_free(r);
        return 0;
    }
    return 1;
}

static void drop_oldest(rewind_t *r) {
    rewind_entry_t *e = &r->entries[r->head];
    r->bytes -= e->size;
    free(e->data);
    e->data = NULL;
    r->head = (r->head + 1u) % r->capacity;
    r->count--;
}

void rewind_clear(rewind_t *r) {
    while (r->count > 0) {
        drop_oldest(r);
    }
    r->head = 0;
    r->has_newest = 0;
}

void rewind_free(rewind_t *r) {
    if (r->entries) {
        rewind_clear(r);
    }
    free(r->newest);
    free(r->scratch);
    free(r->entries);
    memset(r, 0, sizeof(*r));
}

void rewind_push(rewind_t *r, const uint8_t *state, uint32_t frame) {
    if (!r->newest) {
        return;
    }
    if (r->has_newest && r->capacity > 0) {
        size_t size = delta_encode(r->newest, state, r->state_size, r->scratch);
        uint8_t *data = (uint8_t *)malloc(size ? size : 1u);
        if (!data) {
            /* No memory: the history up to this frame is lost. The new state is still the newest. */
            rewind_clear(r);
        } else {
            rewind_entry_t *e;
            memcpy(data, r->scratch, size);
            if (r->count == r->capacity) {
                drop_oldest(r);
            }
            e = &r->entries[(r->head + r->count) % r->capacity];
            e->data = data;
            e->size = size;
            e->frame = r->newest_frame;
            r->count++;
            r->bytes += size;
            while (r->count > 1 && r->bytes > r->byte_budget) {
                drop_oldest(r);
            }
        }
    }
    memcpy(r->newest, state, r->state_size);
    r->newest_frame = frame;
    r->has_newest = 1;
}

int rewind_pop(rewind_t *r, uint8_t *out, uint32_t *out_frame) {
    if (!r->has_newest) {
        return 0;
    }
    memcpy(out, r->newest, r->state_size);
    *out_frame = r->newest_frame;
    if (r->count > 0) {
        /* The XOR of two states gives each one from the other. Thus the difference that rewind_push
           made from the older state and the newer state now gives the older state. */
        rewind_entry_t *e = &r->entries[(r->head + r->count - 1u) % r->capacity];
        delta_apply(r->newest, e->data, e->size);
        r->newest_frame = e->frame;
        r->bytes -= e->size;
        free(e->data);
        e->data = NULL;
        r->count--;
    } else {
        r->has_newest = 0;
    }
    return 1;
}
