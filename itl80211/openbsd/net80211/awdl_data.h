/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL80211_AWDL_DATA_H
#define ITL80211_AWDL_DATA_H
#include "awdl_frame.h"
struct itl_awdl_data {
    uint8_t source[6], destination[6];
    uint16_t sequence, ether_type;
    size_t payload_offset, payload_length;
};

/* Writes only the LLC/SNAP + AWDL data header, not an 802.11 frame or payload.
 * Output is untouched on error. Sequence allocation belongs to the caller.
 */
static inline enum itl_awdl_result
itl_awdl_write_data_header(uint8_t *out, size_t capacity, uint16_t sequence, uint16_t ether_type)
{
    uint8_t header[16] = {0xaa,0xaa,3,0,0x17,0xf2,8,0,3,4,0,0,0,0,0,0};
    if (!out || ether_type < 0x0600) return ITL_AWDL_MALFORMED;
    if (capacity < sizeof(header)) return ITL_AWDL_TRUNCATED;
    header[10] = (uint8_t)sequence;
    header[11] = (uint8_t)(sequence >> 8);
    header[14] = (uint8_t)(ether_type >> 8);
    header[15] = (uint8_t)ether_type;
    memcpy(out, header, sizeof(header));
    return ITL_AWDL_VALID;
}

/* A borrowed FCS-free 802.11 frame, without radiotap. Payload remains opaque:
 * no IPv6 validation, peer authentication, replay filtering or delivery here.
 * No allocation, retained pointer, packed access or mutation of the reader.
 */
static inline enum itl_awdl_result
itl_awdl_parse_data(const struct itl_awdl_reader *reader, struct itl_awdl_data *out)
{
    const uint8_t bssid[6] = {0,0x25,0,0xff,0x94,0x73};
    const uint8_t llc[8] = {0xaa,0xaa,3,0,0x17,0xf2,8,0};
    uint8_t header[24], qos[2], body[16];
    size_t offset = sizeof(header);
    struct itl_awdl_data parsed = {0};
    if (!out) return ITL_AWDL_MALFORMED;
    memset(out, 0, sizeof(*out));
    if (!reader || !reader->read || reader->length > 4096) return ITL_AWDL_MALFORMED;
    if (reader->length < sizeof(header) ||
        reader->read(reader->context, 0, header, sizeof(header)))
        return ITL_AWDL_TRUNCATED;
    if (header[0] != 0x08 && header[0] != 0x88) return ITL_AWDL_NOT_AWDL;
    if ((header[1] & 0x47) || (header[22] & 0x0f)) return ITL_AWDL_UNSUPPORTED;
    if (memcmp(header + 16, bssid, sizeof(bssid))) return ITL_AWDL_NOT_AWDL;
    if (header[0] == 0x88) {
        if (sizeof(qos) > reader->length - offset ||
            reader->read(reader->context, offset, qos, sizeof(qos)))
            return ITL_AWDL_TRUNCATED;
        if (qos[0] & 0x80) return ITL_AWDL_UNSUPPORTED; // A-MSDU requires subframe parsing.
        offset += sizeof(qos);
        if (header[1] & 0x80) offset += 4; // HT Control follows QoS Control.
    }
    if (offset > reader->length || sizeof(body) > reader->length - offset ||
        reader->read(reader->context, offset, body, sizeof(body)))
        return ITL_AWDL_TRUNCATED;
    if (memcmp(body, llc, sizeof(llc))) return ITL_AWDL_NOT_AWDL;
    if (body[8] != 3 || body[9] != 4 || body[12] || body[13])
        return ITL_AWDL_UNSUPPORTED;
    parsed.sequence = (uint16_t)((uint16_t)body[10] | ((uint16_t)body[11] << 8));
    parsed.ether_type = (uint16_t)(((uint16_t)body[14] << 8) | body[15]);
    if (parsed.ether_type < 0x0600) return ITL_AWDL_MALFORMED;
    memcpy(parsed.destination, header + 4, 6);
    memcpy(parsed.source, header + 10, 6);
    parsed.payload_offset = offset + sizeof(body);
    parsed.payload_length = reader->length - parsed.payload_offset;
    *out = parsed;
    return ITL_AWDL_VALID;
}
#endif
