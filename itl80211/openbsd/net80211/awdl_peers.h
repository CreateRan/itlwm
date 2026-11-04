/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL80211_AWDL_PEERS_H
#define ITL80211_AWDL_PEERS_H
#include "awdl_sync.h"
enum { ITL_AWDL_PEER_LIMIT = 16 };
enum itl_awdl_peer_result {
    ITL_AWDL_PEER_OK, ITL_AWDL_PEER_INVALID, ITL_AWDL_PEER_CLOCK_REVERSED,
    ITL_AWDL_PEER_DUPLICATE, ITL_AWDL_PEER_OLD, ITL_AWDL_PEER_FULL,
    ITL_AWDL_PEER_MISSING, ITL_AWDL_PEER_NO_SYNC, ITL_AWDL_PEER_STALE
};
struct itl_awdl_peer {
    uint8_t used, has_sync, address[6];
    uint16_t last_sequence;
    uint64_t last_seen_us, sync_received_us;
    struct itl_awdl_sync sync;
};
struct itl_awdl_peers {
    uint8_t configured, has_clock, has_reference, reference_index, self[6];
    uint64_t last_now_us;
    struct itl_awdl_peer entries[ITL_AWDL_PEER_LIMIT];
};
struct itl_awdl_peer_window {
    uint8_t address[6];
    uint64_t observation_us, age_us;
    struct itl_awdl_window window;
};
/* Caller owns storage and serializes every access. No allocation or live RX
 * integration. MAC selection and sequence ordering are not authentication.
 */
static inline int itl_awdl_peer_valid_address(const uint8_t *address)
{
    uint8_t bits = 0;
    if (!address || (address[0] & 1)) return 0;
    for (unsigned i = 0; i < 6; ++i) bits |= address[i];
    return bits != 0;
}
static inline enum itl_awdl_peer_result
itl_awdl_peers_init(struct itl_awdl_peers *table, const uint8_t *self)
{
    uint8_t address[6];
    if (!table || !itl_awdl_peer_valid_address(self)) return ITL_AWDL_PEER_INVALID;
    memcpy(address,self,6);
    memset(table,0,sizeof(*table));
    memcpy(table->self,address,6); table->configured = 1;
    return ITL_AWDL_PEER_OK;
}
static inline int itl_awdl_peers_find(const struct itl_awdl_peers *table, const uint8_t *address)
{
    if (!table || table->configured != 1 || !address) return -1;
    for (unsigned i = 0; i < ITL_AWDL_PEER_LIMIT; ++i)
        if (table->entries[i].used && !memcmp(table->entries[i].address,address,6)) return (int)i;
    return -1;
}
static inline void itl_awdl_peers_remove_slot(struct itl_awdl_peers *table, unsigned slot)
{
    if (!table || slot >= ITL_AWDL_PEER_LIMIT) return;
    if (table->has_reference && table->reference_index == slot) {
        table->has_reference = 0; table->reference_index = 0;
    }
    memset(&table->entries[slot],0,sizeof(table->entries[slot]));
}
static inline enum itl_awdl_peer_result
itl_awdl_peers_expire(struct itl_awdl_peers *table, uint64_t now, uint64_t ttl)
{
    if (!table || table->configured != 1) return ITL_AWDL_PEER_INVALID;
    if (table->has_clock && now < table->last_now_us) return ITL_AWDL_PEER_CLOCK_REVERSED;
    table->has_clock = 1; table->last_now_us = now;
    for (unsigned i = 0; i < ITL_AWDL_PEER_LIMIT; ++i)
        if (table->entries[i].used && now - table->entries[i].last_seen_us > ttl)
            itl_awdl_peers_remove_slot(table,i);
    return ITL_AWDL_PEER_OK;
}
static inline enum itl_awdl_peer_result
itl_awdl_peers_observe(struct itl_awdl_peers *table,
    const struct itl_awdl_reader *reader, uint64_t received_us, uint64_t ttl)
{
    struct itl_awdl_action action;
    struct itl_awdl_sync sync;
    uint8_t sequence_bytes[2];
    uint16_t sequence;
    enum itl_awdl_peer_result result;
    int slot;
    if (!table || table->configured != 1) return ITL_AWDL_PEER_INVALID;
    // Validate every borrowed input before changing table time, expiry or state.
    if (itl_awdl_parse_action(reader,&action) != ITL_AWDL_VALID ||
        !itl_awdl_peer_valid_address(action.source) || !memcmp(action.source,table->self,6) ||
        itl_awdl_parse_sync(reader,&sync) != ITL_AWDL_VALID ||
        reader->read(reader->context,22,sequence_bytes,sizeof(sequence_bytes)))
        return ITL_AWDL_PEER_INVALID;
    sequence = (uint16_t)(itl_awdl_sync_le16(sequence_bytes) >> 4);
    result = itl_awdl_peers_expire(table,received_us,ttl);
    if (result != ITL_AWDL_PEER_OK) return result;
    slot = itl_awdl_peers_find(table,action.source);
    if (slot >= 0) {
        uint16_t delta = (uint16_t)((sequence + 4096U - table->entries[slot].last_sequence) & 4095U);
        if (!delta) return ITL_AWDL_PEER_DUPLICATE;
        // The exact half-range is ambiguous; conservatively reject it too.
        if (delta >= 2048) return ITL_AWDL_PEER_OLD;
    } else {
        for (unsigned i = 0; i < ITL_AWDL_PEER_LIMIT; ++i)
            if (!table->entries[i].used) { slot = (int)i; break; }
        if (slot < 0) return ITL_AWDL_PEER_FULL;
        table->entries[slot].used = 1;
        memcpy(table->entries[slot].address,action.source,6);
    }
    struct itl_awdl_peer *peer = &table->entries[slot];
    peer->last_sequence = sequence; peer->last_seen_us = received_us;
    if (sync.has_sync && sync.has_sequence) {
        peer->sync = sync; peer->sync_received_us = received_us; peer->has_sync = 1;
    } else if (sync.has_sync || sync.has_sequence) {
        // Never merge separate-frame partial updates into a fresh clock anchor.
        peer->has_sync = 0; peer->sync_received_us = 0;
        memset(&peer->sync,0,sizeof(peer->sync));
    }
    // Unrelated discovery refreshes liveness only, never sync_received_us.
    return ITL_AWDL_PEER_OK;
}
static inline enum itl_awdl_peer_result
itl_awdl_peers_select(struct itl_awdl_peers *table, const uint8_t *address,
                      uint64_t now, uint64_t ttl, uint64_t sync_age)
{
    enum itl_awdl_peer_result result;
    int slot;
    if (!itl_awdl_peer_valid_address(address)) return ITL_AWDL_PEER_INVALID;
    result = itl_awdl_peers_expire(table,now,ttl);
    if (result != ITL_AWDL_PEER_OK) return result;
    slot = itl_awdl_peers_find(table,address);
    if (slot < 0) return ITL_AWDL_PEER_MISSING;
    if (!table->entries[slot].has_sync) return ITL_AWDL_PEER_NO_SYNC;
    if (now - table->entries[slot].sync_received_us > sync_age) return ITL_AWDL_PEER_STALE;
    table->reference_index = (uint8_t)slot; table->has_reference = 1;
    return ITL_AWDL_PEER_OK;
}
static inline enum itl_awdl_peer_result
itl_awdl_peers_project(struct itl_awdl_peers *table, uint64_t now,
                       uint64_t ttl, uint64_t sync_age, struct itl_awdl_peer_window *out)
{
    struct itl_awdl_peer_window projected = {0};
    enum itl_awdl_peer_result result;
    if (!out) return ITL_AWDL_PEER_INVALID;
    memset(out,0,sizeof(*out));
    result = itl_awdl_peers_expire(table,now,ttl);
    if (result != ITL_AWDL_PEER_OK) return result;
    if (!table->has_reference) return ITL_AWDL_PEER_MISSING;
    struct itl_awdl_peer *peer = &table->entries[table->reference_index];
    if (!peer->has_sync) return ITL_AWDL_PEER_NO_SYNC;
    if (now - peer->sync_received_us > sync_age) return ITL_AWDL_PEER_STALE;
    if (itl_awdl_project_window(&peer->sync,peer->sync_received_us,now,sync_age,&projected.window) != ITL_AWDL_VALID)
        return ITL_AWDL_PEER_INVALID;
    memcpy(projected.address,peer->address,6);
    projected.observation_us = peer->sync_received_us;
    projected.age_us = now - peer->sync_received_us;
    *out = projected;
    return ITL_AWDL_PEER_OK;
}
static inline enum itl_awdl_peer_result
itl_awdl_peers_forget(struct itl_awdl_peers *table, const uint8_t *address)
{
    if (!table || table->configured != 1 || !itl_awdl_peer_valid_address(address))
        return ITL_AWDL_PEER_INVALID;
    int slot = itl_awdl_peers_find(table,address);
    if (slot < 0) return ITL_AWDL_PEER_MISSING;
    itl_awdl_peers_remove_slot(table,(unsigned)slot);
    return ITL_AWDL_PEER_OK;
}
static inline void itl_awdl_peers_clear(struct itl_awdl_peers *table)
{
    if (!table || table->configured != 1) return;
    memset(table->entries,0,sizeof(table->entries));
    table->has_reference = 0; table->reference_index = 0;
    // Preserve local identity and monotonic-time watermark across peer clearing.
}
#endif
