/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL80211_AWDL_FRAME_H
#define ITL80211_AWDL_FRAME_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A borrowed, FCS-free 802.11 frame. read returns zero on success. */
struct itl_awdl_reader {
    void *context;
    size_t length;
    int (*read)(void *, size_t, void *, size_t);
};
enum itl_awdl_result {
    ITL_AWDL_NOT_AWDL, ITL_AWDL_VALID, ITL_AWDL_TRUNCATED,
    ITL_AWDL_UNSUPPORTED, ITL_AWDL_MALFORMED
};
struct itl_awdl_action {
    uint8_t source[6], destination[6];
    uint8_t version, subtype;
    uint32_t phy_tx, target_tx;
    uint16_t tlv_count;
    uint32_t tlv_types;
};
static inline uint32_t itl_awdl_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* No allocation, packed-struct casts, retained buffers, or frame mutations.
 * Unknown TLVs are skipped by their validated little-endian length.
 * FCS/radiotap removal and decryption belong to the caller.
 */
static inline enum itl_awdl_result
itl_awdl_parse_action(const struct itl_awdl_reader *reader,
                      struct itl_awdl_action *out)
{
    uint8_t header[24], action[16], tl[3];
    size_t offset;
    struct itl_awdl_action parsed = {0};
    if (!out) return ITL_AWDL_MALFORMED;
    memset(out, 0, sizeof(*out));
    if (!reader || !reader->read || reader->length > 4096)
        return ITL_AWDL_MALFORMED;
    if (reader->length < sizeof(header) ||
        reader->read(reader->context, 0, header, sizeof(header)))
        return ITL_AWDL_TRUNCATED;
    if (header[0] != 0xd0) return ITL_AWDL_NOT_AWDL;
    if (header[1] & 0x03) return ITL_AWDL_MALFORMED;
    /* No reassembly or decryption in this observer. */
    if ((header[1] & 0x44) || (header[22] & 0x0f))
        return ITL_AWDL_UNSUPPORTED;
    offset = sizeof(header) + ((header[1] & 0x80) ? 4 : 0);
    if (offset > reader->length || sizeof(action) > reader->length - offset ||
        reader->read(reader->context, offset, action, sizeof(action)))
        return ITL_AWDL_TRUNCATED;
    if (action[0] != 0x7f || action[1] != 0 || action[2] != 0x17 ||
        action[3] != 0xf2 || action[4] != 8)
        return ITL_AWDL_NOT_AWDL;
    if (action[5] != 0x10 || (action[6] != 0 && action[6] != 3))
        return ITL_AWDL_UNSUPPORTED;
    memcpy(parsed.destination, header + 4, 6);
    memcpy(parsed.source, header + 10, 6);
    parsed.version = action[5];
    parsed.subtype = action[6];
    parsed.phy_tx = itl_awdl_le32(action + 8);
    parsed.target_tx = itl_awdl_le32(action + 12);
    offset += sizeof(action);
    while (offset < reader->length) {
        size_t length;
        if (sizeof(tl) > reader->length - offset ||
            reader->read(reader->context, offset, tl, sizeof(tl)))
            return ITL_AWDL_TRUNCATED;
        offset += sizeof(tl);
        length = (size_t)tl[1] | ((size_t)tl[2] << 8);
        if (length > reader->length - offset) return ITL_AWDL_TRUNCATED;
        ++parsed.tlv_count;
        if (tl[0] < 32) parsed.tlv_types |= (uint32_t)1 << tl[0];
        offset += length;
    }
    *out = parsed;
    return ITL_AWDL_VALID;
}
#endif
