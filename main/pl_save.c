// SPDX-License-Identifier: Apache-2.0
#include "pl_save.h"

#include <string.h>

enum { OFF_VERSION = 4, OFF_POET = 6, OFF_PAGE = 8, OFF_TURN = 10, OFF_CHECKSUM = 38 };
static const uint8_t s_magic[4] = {'P', 'L', 'S', 'V'};
#define PL_SAVE_VERSION 1

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)(v & 0xFFFFu));
    put16(p + 2, (uint16_t)(v >> 16));
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}

static uint16_t checksum(const uint8_t *b)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < OFF_CHECKSUM; i++) sum = (uint16_t)(sum + b[i]);
    return sum;
}

void pl_save_capture(pl_save_data_t *out, const poets_game_t *core, int poet_index,
                     int page)
{
    memset(out, 0, sizeof(*out));
    out->poet_index = (int16_t)poet_index;
    out->page = (int16_t)page;
    out->turn = core->turn;
    out->last_category = core->last_category;
    out->dead = core->dead ? 1 : 0;
    out->death_reason = core->death_reason;
    out->has_rank = core->has_rank ? 1 : 0;
    out->poems = core->poems;
    out->silver = core->silver;
    out->food = core->food;
    out->fame = core->fame;
    out->health = core->health;
    out->muse = core->muse;
    out->friends = core->friends;
    out->rng = core->rng;
}

void pl_save_restore(const pl_save_data_t *in, poets_game_t *core)
{
    core->turn = in->turn;
    core->last_category = in->last_category;
    core->dead = in->dead != 0;
    core->death_reason = in->death_reason;
    core->has_rank = in->has_rank != 0;
    core->poems = in->poems;
    core->silver = in->silver;
    core->food = in->food;
    core->fame = in->fame;
    core->health = in->health;
    core->muse = in->muse;
    core->friends = in->friends;
    core->rng = in->rng;
}

size_t pl_save_encode(const pl_save_data_t *in, uint8_t *bytes, size_t capacity)
{
    if (capacity < PL_SAVE_BYTES) return 0;
    memset(bytes, 0, PL_SAVE_BYTES);
    memcpy(bytes, s_magic, 4);
    put16(bytes + OFF_VERSION, PL_SAVE_VERSION);
    put16(bytes + OFF_POET, (uint16_t)in->poet_index);
    put16(bytes + OFF_PAGE, (uint16_t)in->page);
    bytes[OFF_TURN] = in->turn;
    bytes[11] = in->last_category;
    bytes[12] = in->dead;
    bytes[13] = in->death_reason;
    bytes[14] = in->has_rank;
    put16(bytes + 16, in->poems);
    put16(bytes + 18, (uint16_t)in->silver);
    put16(bytes + 20, (uint16_t)in->food);
    put16(bytes + 22, (uint16_t)in->fame);
    put16(bytes + 24, (uint16_t)in->health);
    put16(bytes + 26, (uint16_t)in->muse);
    put16(bytes + 28, (uint16_t)in->friends);
    put32(bytes + 30, in->rng);
    put16(bytes + OFF_CHECKSUM, checksum(bytes));
    return PL_SAVE_BYTES;
}

bool pl_save_decode(const uint8_t *bytes, size_t size, pl_save_data_t *out)
{
    if (size != PL_SAVE_BYTES || memcmp(bytes, s_magic, 4) != 0) return false;
    if (get16(bytes + OFF_VERSION) != PL_SAVE_VERSION) return false;
    if (get16(bytes + OFF_CHECKSUM) != checksum(bytes)) return false;
    memset(out, 0, sizeof(*out));
    out->poet_index = (int16_t)get16(bytes + OFF_POET);
    out->page = (int16_t)get16(bytes + OFF_PAGE);
    out->turn = bytes[OFF_TURN];
    out->last_category = bytes[11];
    out->dead = bytes[12];
    out->death_reason = bytes[13];
    out->has_rank = bytes[14];
    out->poems = get16(bytes + 16);
    out->silver = (int16_t)get16(bytes + 18);
    out->food = (int16_t)get16(bytes + 20);
    out->fame = (int16_t)get16(bytes + 22);
    out->health = (int16_t)get16(bytes + 24);
    out->muse = (int16_t)get16(bytes + 26);
    out->friends = (int16_t)get16(bytes + 28);
    out->rng = get32(bytes + 30);
    return out->poet_index >= 0 && out->poet_index < POETS_POET_COUNT
        && out->page >= 0 && out->page < PL_PAGE_COUNT;
}
