/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL_IWX_FLUSH_V1_H
#define ITL_IWX_FLUSH_V1_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum { ITL_IWX_FLUSH_QUEUE_LIMIT = 16, ITL_IWX_FLUSH_RESPONSE_SIZE = 132 };
enum itl_iwx_flush_result { ITL_IWX_FLUSH_OK, ITL_IWX_FLUSH_INVALID, ITL_IWX_FLUSH_NO_REPLY };
struct itl_iwx_flush_entry { uint16_t tid, queue, read_before, read_after; };
struct itl_iwx_flush_response { uint16_t count; struct itl_iwx_flush_entry entries[16]; };
/* Legacy TX_PATH_FLUSH_CMD_RSP_API_S_VER_1: fixed array, not a variable
 * response or an acknowledgement that a queue was removed. Caller serializes
 * ownership/ring updates and supplies the original station and requested mask.
 * Output must be separate from input and is cleared on any incomplete reply.
 */
static inline uint16_t itl_iwx_flush_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline enum itl_iwx_flush_result
itl_iwx_flush_decode(const void *bytes, size_t length, uint32_t station,
    uint16_t requested_tids, struct itl_iwx_flush_response *out)
{
    struct itl_iwx_flush_response decoded;
    if (!out)
        return ITL_IWX_FLUSH_INVALID;
    memset(out, 0, sizeof(*out));
    if (station > UINT16_MAX)
        return ITL_IWX_FLUSH_INVALID;
    if (!length)
        return ITL_IWX_FLUSH_NO_REPLY;
    if (!bytes || length != ITL_IWX_FLUSH_RESPONSE_SIZE)
        return ITL_IWX_FLUSH_INVALID;
    const uint8_t *p = (const uint8_t *)bytes;
    if (itl_iwx_flush_le16(p) != station)
        return ITL_IWX_FLUSH_INVALID;
    memset(&decoded, 0, sizeof(decoded));
    decoded.count = itl_iwx_flush_le16(p + 2);
    if (decoded.count > ITL_IWX_FLUSH_QUEUE_LIMIT)
        return ITL_IWX_FLUSH_INVALID;
    for (unsigned i = 0; i < decoded.count; ++i) {
        const uint8_t *wire = p + 4 + i * 8;
        struct itl_iwx_flush_entry *entry = &decoded.entries[i];
        entry->tid = itl_iwx_flush_le16(wire);
        entry->queue = itl_iwx_flush_le16(wire + 2);
        entry->read_before = itl_iwx_flush_le16(wire + 4);
        entry->read_after = itl_iwx_flush_le16(wire + 6);
        if (entry->tid > 15 || entry->queue >= 512 ||
            !(requested_tids & (1U << entry->tid)))
            return ITL_IWX_FLUSH_INVALID;
        for (unsigned j = 0; j < i; ++j)
            if (decoded.entries[j].queue == entry->queue)
                return ITL_IWX_FLUSH_INVALID;
    }
    *out = decoded;
    return ITL_IWX_FLUSH_OK;
}

/* TID 15 belongs to the separately tracked default/management queue, not
 * data_queues[15]. Unsupported data TIDs have no software binding. This tests
 * an identity snapshot only; the caller must still validate live ring state.
 */
static inline int
itl_iwx_flush_binding_matches(uint16_t tid, uint16_t queue,
    const int32_t *data_queues, size_t data_count, int32_t management_queue)
{
    if (!queue) /* Command queue 0 never belongs to a station binding. */
        return 0;
    if (tid == 15)
        return management_queue >= 0 && (uint32_t)management_queue == queue;
    if (!data_queues || tid >= data_count || tid >= 15)
        return 0;
    return data_queues[tid] >= 0 && (uint32_t)data_queues[tid] == queue;
}
#endif
