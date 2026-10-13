#include "movie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* The file format. Each integer is little-endian, with an explicit width:

     0  "PKMV"
     4  u32 file version (MOVIE_FILE_VERSION)
     8  u32 flags (MOVIE_FLAG_*)
    12  u32 rerecord count
    16  u64 movie id
    24  u32 frame count
    28  u32 CRC-32 of the BIOS
    32  u32 content size
    36  u32 content identity hash
    40  u32 start state size
    44  emulator version, MOVIE_EMULATOR_VERSION_SIZE bytes, padded with zero
    76  u32 reserved, zero
    80  the start state, from psemu_save_state
     .  one input byte for each frame

   The start state carries its own version (PSEMU_STATE_VERSION). Thus a build with a different state
   format refuses the movie at movie_load_start_state, and not with a desync later. */
#define MOVIE_MAGIC "PKMV"
#define MOVIE_FILE_VERSION 1u
#define MOVIE_HEADER_SIZE 80u

/* The trailer of a save state:

     0  "PKMT"
     4  u64 movie id
    12  u32 position (frame)
    16  u32 input count
    20  the inputs, one byte for each frame

   The input count is less than the position only after the end of a read-only movie. The machine then
   ran frames with live input that the movie does not hold. */
#define TRAILER_MAGIC "PKMT"
#define TRAILER_HEADER_SIZE 20u

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_u64(uint8_t *p, uint64_t v) {
    put_u32(p, (uint32_t)v);
    put_u32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const uint8_t *p) {
    return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4) << 32);
}

void movie_init(movie_t *m) {
    memset(m, 0, sizeof(*m));
}

void movie_close(movie_t *m) {
    free(m->start_state);
    free(m->inputs);
    movie_init(m);
}

static int reserve_inputs(movie_t *m, uint32_t count) {
    uint32_t cap;
    uint8_t *grown;
    if (count <= m->capacity) {
        return 1;
    }
    /* 32 frames are one second. Thus one hour is approximately 115,000 bytes, and a growth by
       doubling is sufficient. */
    cap = m->capacity ? m->capacity : 4096u;
    while (cap < count) {
        cap *= 2u;
    }
    grown = (uint8_t *)realloc(m->inputs, cap);
    if (!grown) {
        return 0;
    }
    m->inputs = grown;
    m->capacity = cap;
    return 1;
}

/* Puts the mode in agreement with the position, after a change to the position or to read-only. */
static void settle_mode(movie_t *m) {
    if (m->mode == MOVIE_INACTIVE || (m->mode == MOVIE_RECORDING && !m->read_only)) {
        return;
    }
    if (m->frame < m->length) {
        m->mode = MOVIE_PLAYING;
    } else if (!m->read_only && m->frame == m->length) {
        m->mode = MOVIE_RECORDING;
    } else {
        m->mode = MOVIE_FINISHED;
    }
}

int movie_start_recording(movie_t *m, const psemu_t *ps, const char *path, uint64_t id, uint32_t flags,
    uint32_t bios_crc, uint32_t content_size, uint32_t content_hash, const char *emulator_version) {
    size_t state_size = psemu_state_size(ps);
    uint8_t *state = (uint8_t *)malloc(state_size);
    if (!state || psemu_save_state(ps, state, state_size) != PSEMU_OK) {
        free(state);
        return 0;
    }
    movie_close(m);
    m->start_state = state;
    m->start_state_size = state_size;
    snprintf(m->path, sizeof(m->path), "%s", path);
    m->id = id;
    m->flags = flags;
    m->bios_crc = bios_crc;
    m->content_size = content_size;
    m->content_hash = content_hash;
    snprintf(m->emulator_version, sizeof(m->emulator_version), "%s", emulator_version);
    m->mode = MOVIE_RECORDING;
    m->read_only = 0;
    m->dirty = 1;
    return 1;
}

int movie_open(movie_t *m, const char *path, char *err, size_t err_size) {
    FILE *f = fopen(path, "rb");
    uint8_t header[MOVIE_HEADER_SIZE];
    long file_size;
    uint32_t length, state_size;
    uint8_t *state = NULL;
    uint8_t *inputs = NULL;

    if (!f) {
        snprintf(err, err_size, "Couldn't open that file.");
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (file_size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), f) != sizeof(header) || memcmp(header, MOVIE_MAGIC, 4) != 0) {
        snprintf(err, err_size, "That file isn't a Pokket movie.");
        fclose(f);
        return 0;
    }
    if (get_u32(header + 4) != MOVIE_FILE_VERSION) {
        snprintf(err, err_size, "That movie is from a different version of Pokket (movie format %u).",
            (unsigned)get_u32(header + 4));
        fclose(f);
        return 0;
    }
    length = get_u32(header + 24);
    state_size = get_u32(header + 40);
    /* The sizes in the header must give the length of the file exactly. Thus a truncated file, or a
       file with extra bytes, gives an error here and not a short movie. */
    if ((uint64_t)file_size != (uint64_t)MOVIE_HEADER_SIZE + state_size + length || state_size == 0) {
        snprintf(err, err_size, "That movie file is damaged (its size doesn't match its header).");
        fclose(f);
        return 0;
    }
    state = (uint8_t *)malloc(state_size);
    inputs = (uint8_t *)malloc(length ? length : 1u);
    if (!state || !inputs || fread(state, 1, state_size, f) != state_size ||
        (length && fread(inputs, 1, length, f) != length)) {
        snprintf(err, err_size, "Couldn't read that movie file.");
        free(state);
        free(inputs);
        fclose(f);
        return 0;
    }
    fclose(f);

    movie_close(m);
    snprintf(m->path, sizeof(m->path), "%s", path);
    m->flags = get_u32(header + 8);
    m->rerecords = get_u32(header + 12);
    m->id = get_u64(header + 16);
    m->bios_crc = get_u32(header + 28);
    m->content_size = get_u32(header + 32);
    m->content_hash = get_u32(header + 36);
    memcpy(m->emulator_version, header + 44, MOVIE_EMULATOR_VERSION_SIZE);
    m->emulator_version[MOVIE_EMULATOR_VERSION_SIZE - 1] = '\0';
    m->start_state = state;
    m->start_state_size = state_size;
    m->inputs = inputs;
    m->length = length;
    m->capacity = length ? length : 1u;
    m->frame = 0;
    m->read_only = 1;
    m->mode = MOVIE_PLAYING;
    settle_mode(m);
    return 1;
}

psemu_status movie_load_start_state(const movie_t *m, psemu_t *ps) {
    if (!m->start_state) {
        return PSEMU_ERR_BAD_FORMAT;
    }
    return psemu_load_state(ps, m->start_state, m->start_state_size);
}

int movie_write(movie_t *m) {
    char tmp_path[1088];
    uint8_t header[MOVIE_HEADER_SIZE];
    FILE *f;
    int ok;

    memset(header, 0, sizeof(header));
    memcpy(header, MOVIE_MAGIC, 4);
    put_u32(header + 4, MOVIE_FILE_VERSION);
    put_u32(header + 8, m->flags);
    put_u32(header + 12, m->rerecords);
    put_u64(header + 16, m->id);
    put_u32(header + 24, m->length);
    put_u32(header + 28, m->bios_crc);
    put_u32(header + 32, m->content_size);
    put_u32(header + 36, m->content_hash);
    put_u32(header + 40, (uint32_t)m->start_state_size);
    memcpy(header + 44, m->emulator_version, strnlen(m->emulator_version, MOVIE_EMULATOR_VERSION_SIZE - 1));

    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", m->path);
    f = fopen(tmp_path, "wb");
    if (!f) {
        return 0;
    }
    ok = fwrite(header, 1, sizeof(header), f) == sizeof(header) &&
         fwrite(m->start_state, 1, m->start_state_size, f) == m->start_state_size &&
         (m->length == 0 || fwrite(m->inputs, 1, m->length, f) == m->length);
    if (fclose(f) != 0) {
        ok = 0;
    }
    if (!ok || !MoveFileExA(tmp_path, m->path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(tmp_path);
        return 0;
    }
    m->dirty = 0;
    return 1;
}

uint8_t movie_next_input(movie_t *m, uint8_t live) {
    uint8_t input;
    switch (m->mode) {
    case MOVIE_RECORDING:
        if (!reserve_inputs(m, m->frame + 1u)) {
            /* No memory for the frame. The frame runs with the live input, and the movie stops at
               its last complete frame. The caller sees the change of mode. */
            m->mode = MOVIE_FINISHED;
            m->read_only = 1;
            m->frame++;
            return live;
        }
        m->inputs[m->frame] = live;
        m->frame++;
        m->length = m->frame;
        m->dirty = 1;
        return live;
    case MOVIE_PLAYING:
        input = m->inputs[m->frame];
        m->frame++;
        settle_mode(m);
        return input;
    case MOVIE_FINISHED:
        m->frame++;
        return live;
    default:
        return live;
    }
}

void movie_set_read_only(movie_t *m, int read_only) {
    m->read_only = read_only ? 1 : 0;
    settle_mode(m);
}

void movie_seek(movie_t *m, uint32_t frame, int count_rerecord) {
    if (m->mode == MOVIE_INACTIVE) {
        return;
    }
    if (!m->read_only && frame <= m->length) {
        m->length = frame;
        m->frame = frame;
        m->mode = MOVIE_RECORDING;
        if (count_rerecord) {
            m->rerecords++;
        }
        m->dirty = 1;
        return;
    }
    m->frame = frame;
    settle_mode(m);
}

/* The inputs that a state at the current position must hold. After the end of a read-only movie,
   that is the full movie. */
static uint32_t trailer_input_count(const movie_t *m) {
    return m->frame < m->length ? m->frame : m->length;
}

size_t movie_trailer_size(const movie_t *m) {
    return TRAILER_HEADER_SIZE + trailer_input_count(m);
}

void movie_write_trailer(const movie_t *m, uint8_t *buf) {
    uint32_t count = trailer_input_count(m);
    memcpy(buf, TRAILER_MAGIC, 4);
    put_u64(buf + 4, m->id);
    put_u32(buf + 12, m->frame);
    put_u32(buf + 16, count);
    if (count) {
        memcpy(buf + TRAILER_HEADER_SIZE, m->inputs, count);
    }
}

int movie_trailer_is_valid(const uint8_t *buf, size_t size) {
    uint32_t frame, count;
    if (size < TRAILER_HEADER_SIZE || memcmp(buf, TRAILER_MAGIC, 4) != 0) {
        return 0;
    }
    frame = get_u32(buf + 12);
    count = get_u32(buf + 16);
    return (uint64_t)TRAILER_HEADER_SIZE + count == size && count <= frame;
}

movie_trailer_result_t movie_check_trailer(const movie_t *m, const uint8_t *buf, size_t size) {
    uint32_t frame, count;
    if (!movie_trailer_is_valid(buf, size)) {
        return MOVIE_TRAILER_BAD;
    }
    frame = get_u32(buf + 12);
    count = get_u32(buf + 16);
    if (get_u64(buf + 4) != m->id) {
        return MOVIE_TRAILER_OTHER_MOVIE;
    }
    if (m->read_only) {
        /* The inputs of the state must be the start of this movie. A state after the end of the movie
           must hold the full movie. */
        if (count > m->length || (count < frame && count != m->length) ||
            (count && memcmp(buf + TRAILER_HEADER_SIZE, m->inputs, count) != 0)) {
            return MOVIE_TRAILER_NOT_IN_TIMELINE;
        }
        return MOVIE_TRAILER_OK;
    }
    if (count < frame) {
        return MOVIE_TRAILER_PAST_END;
    }
    return MOVIE_TRAILER_OK;
}

int movie_apply_trailer(movie_t *m, const uint8_t *buf, size_t size) {
    uint32_t frame = get_u32(buf + 12);
    uint32_t count = get_u32(buf + 16);
    (void)size;
    if (m->read_only) {
        m->frame = frame;
        settle_mode(m);
        return 1;
    }
    if (!reserve_inputs(m, count)) {
        return 0;
    }
    if (count) {
        memcpy(m->inputs, buf + TRAILER_HEADER_SIZE, count);
    }
    m->length = count;
    m->frame = frame;
    m->mode = MOVIE_RECORDING;
    m->rerecords++;
    m->dirty = 1;
    return 1;
}

uint32_t movie_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int bit;
    for (i = 0; i < size; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
