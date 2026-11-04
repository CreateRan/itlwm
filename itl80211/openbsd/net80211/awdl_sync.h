/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL80211_AWDL_SYNC_H
#define ITL80211_AWDL_SYNC_H
#include "awdl_frame.h"
struct itl_awdl_channel_sequence {
    uint8_t encoding;
    uint8_t channel[16], auxiliary[16];
};
struct itl_awdl_sync {
    uint8_t has_sync, has_sequence;
    uint8_t next_channel, master_channel, guard_time, presence_mode;
    uint8_t master[6];
    uint16_t aw_period_tu, af_period_tu, tx_down_counter_tu, aw_counter;
    struct itl_awdl_channel_sequence sequence;
};
struct itl_awdl_window {
    uint16_t aw_counter;
    uint8_t sequence_index, channel, auxiliary, encoding;
    uint32_t to_next_aw_us, to_next_eaw_us;
};
static inline uint16_t itl_awdl_sync_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Narrow profile: 16 entries, no duplication, four AWs per entry, fill=FFFF.
 * Auxiliary preserves operating class (encoding3) or flags (encoding1).
 */
static inline enum itl_awdl_result
itl_awdl_parse_sequence(const struct itl_awdl_reader *reader, size_t offset,
                         size_t length, size_t padding,
                         struct itl_awdl_channel_sequence *out)
{
    uint8_t header[6], entry[2], pad;
    struct itl_awdl_channel_sequence parsed = {0};
    size_t width, expected;
    if (!out) return ITL_AWDL_MALFORMED;
    memset(out, 0, sizeof(*out));
    if (!reader || !reader->read) return ITL_AWDL_MALFORMED;
    if (offset > reader->length || length > reader->length - offset || length < sizeof(header))
        return ITL_AWDL_TRUNCATED;
    if (reader->read(reader->context, offset, header, sizeof(header))) return ITL_AWDL_TRUNCATED;
    if (header[0] != 15 || header[2] != 0 || header[3] != 3 ||
        header[4] != 0xff || header[5] != 0xff) return ITL_AWDL_UNSUPPORTED;
    if (header[1] == 0) width = 1;
    else if (header[1] == 1 || header[1] == 3) width = 2;
    else return ITL_AWDL_UNSUPPORTED;
    expected = sizeof(header) + 16 * width;
    if (length < expected) return ITL_AWDL_TRUNCATED;
    if (length != expected && (padding > 3 || length != expected + padding)) return ITL_AWDL_MALFORMED;
    parsed.encoding = header[1];
    for (size_t i = 0; i < 16; ++i) {
        if (reader->read(reader->context, offset + sizeof(header) + i * width, entry, width))
            return ITL_AWDL_TRUNCATED;
        if (parsed.encoding == 1) {
            parsed.channel[i] = entry[1]; parsed.auxiliary[i] = entry[0];
        } else {
            parsed.channel[i] = entry[0]; parsed.auxiliary[i] = width == 2 ? entry[1] : 0;
        }
    }
    for (size_t i = expected; i < length; ++i) {
        if (reader->read(reader->context, offset + i, &pad, 1)) return ITL_AWDL_TRUNCATED;
        if (pad) return ITL_AWDL_MALFORMED;
    }
    *out = parsed;
    return ITL_AWDL_VALID;
}

/* Parse a complete discovery frame without adopting a peer or a clock.
 * Generic validation precedes the semantic pass; all results commit atomically.
 */
static inline enum itl_awdl_result
itl_awdl_parse_sync(const struct itl_awdl_reader *reader, struct itl_awdl_sync *out)
{
    struct itl_awdl_action action;
    struct itl_awdl_sync parsed = {0};
    enum itl_awdl_result result;
    uint8_t fc[2], tl[3], fixed[33];
    size_t offset;
    int standalone_seen = 0;
    if (!out) return ITL_AWDL_MALFORMED;
    memset(out, 0, sizeof(*out));
    result = itl_awdl_parse_action(reader, &action);
    if (result != ITL_AWDL_VALID) return result;
    if (reader->read(reader->context, 0, fc, sizeof(fc))) return ITL_AWDL_TRUNCATED;
    offset = 40 + ((fc[1] & 0x80) ? 4 : 0);
    while (offset < reader->length) {
        struct itl_awdl_channel_sequence sequence;
        int sequence_present = 0;
        size_t length;
        if (sizeof(tl) > reader->length - offset ||
            reader->read(reader->context, offset, tl, sizeof(tl))) return ITL_AWDL_TRUNCATED;
        length = itl_awdl_sync_le16(tl + 1);
        offset += sizeof(tl);
        if (length > reader->length - offset) return ITL_AWDL_TRUNCATED;
        if (tl[0] == 4) {
            if (parsed.has_sync) return ITL_AWDL_MALFORMED;
            if (length < sizeof(fixed) || reader->read(reader->context, offset, fixed, sizeof(fixed)))
                return ITL_AWDL_TRUNCATED;
            parsed.next_channel = fixed[0];
            parsed.tx_down_counter_tu = itl_awdl_sync_le16(fixed + 1);
            parsed.master_channel = fixed[3]; parsed.guard_time = fixed[4];
            parsed.aw_period_tu = itl_awdl_sync_le16(fixed + 5);
            parsed.af_period_tu = itl_awdl_sync_le16(fixed + 7);
            memcpy(parsed.master, fixed + 21, 6);
            parsed.presence_mode = fixed[27];
            parsed.aw_counter = itl_awdl_sync_le16(fixed + 29);
            if (parsed.presence_mode != 4) return ITL_AWDL_UNSUPPORTED;
            if (!parsed.aw_period_tu || parsed.tx_down_counter_tu > (uint32_t)parsed.aw_period_tu * 4)
                return ITL_AWDL_MALFORMED;
            parsed.has_sync = 1;
            if (length > sizeof(fixed)) {
                result = itl_awdl_parse_sequence(reader, offset + sizeof(fixed), length - sizeof(fixed), 2, &sequence);
                if (result != ITL_AWDL_VALID) return result;
                sequence_present = 1;
            }
        } else if (tl[0] == 18) {
            if (standalone_seen) return ITL_AWDL_MALFORMED;
            standalone_seen = 1;
            result = itl_awdl_parse_sequence(reader, offset, length, 3, &sequence);
            if (result != ITL_AWDL_VALID) return result;
            sequence_present = 1;
        }
        if (sequence_present) {
            if (parsed.has_sequence && memcmp(&parsed.sequence, &sequence, sizeof(sequence)))
                return ITL_AWDL_MALFORMED;
            parsed.sequence = sequence; parsed.has_sequence = 1;
        }
        offset += length;
    }
    *out = parsed;
    return ITL_AWDL_VALID;
}

/* Nominal phase projection only, not a radio scheduler or a synchronization
 * update. Caller must choose/trust the peer, correct RX delay, enforce age and
 * later account for guard time, clock drift, channel eligibility and ownership.
 * All calculations use relative time to avoid underflow near clock startup and
 * overflow near UINT64_MAX. Counter wraps at 16 bits; channel zero means idle.
 */
static inline enum itl_awdl_result
itl_awdl_project_window(const struct itl_awdl_sync *sync, uint64_t received_us,
                        uint64_t now_us, uint64_t max_age_us, struct itl_awdl_window *out)
{
    struct itl_awdl_window projected = {0};
    uint64_t aw_us, period_us, countdown_us, elapsed, phase, groups = 0;
    uint32_t counter;
    if (!out) return ITL_AWDL_MALFORMED;
    memset(out, 0, sizeof(*out));
    if (!sync || now_us < received_us) return ITL_AWDL_MALFORMED;
    if (!sync->has_sync || !sync->has_sequence || sync->presence_mode != 4 ||
        (sync->sequence.encoding != 0 && sync->sequence.encoding != 1 && sync->sequence.encoding != 3))
        return ITL_AWDL_UNSUPPORTED;
    if (!sync->aw_period_tu || sync->tx_down_counter_tu > (uint32_t)sync->aw_period_tu * 4)
        return ITL_AWDL_MALFORMED;
    elapsed = now_us - received_us;
    if (elapsed > max_age_us) return ITL_AWDL_UNSUPPORTED;
    aw_us = (uint64_t)sync->aw_period_tu * 1024;
    period_us = aw_us * 4;
    countdown_us = (uint64_t)sync->tx_down_counter_tu * 1024;
    if (elapsed < countdown_us) phase = period_us - (countdown_us - elapsed);
    else { groups = (elapsed - countdown_us) / period_us + 1; phase = (elapsed - countdown_us) % period_us; }
    counter = (uint32_t)(sync->aw_counter & 0xfffc) + (uint32_t)(groups % 16384) * 4 + (uint32_t)(phase / aw_us);
    projected.aw_counter = (uint16_t)counter;
    projected.sequence_index = (uint8_t)((projected.aw_counter / 4) % 16);
    projected.encoding = sync->sequence.encoding;
    projected.channel = sync->sequence.channel[projected.sequence_index];
    projected.auxiliary = sync->sequence.auxiliary[projected.sequence_index];
    projected.to_next_aw_us = (uint32_t)(aw_us - phase % aw_us);
    projected.to_next_eaw_us = (uint32_t)(period_us - phase);
    *out = projected;
    return ITL_AWDL_VALID;
}
#endif
