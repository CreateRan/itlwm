/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL_IWX_TXQ_V2_H
#define ITL_IWX_TXQ_V2_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Legacy SCD_QUEUE_CFG API v2, not SCD_QUEUE_CONFIG_CMD v3/v4. */
enum { ITL_IWX_TXQ_V2_REQUEST_SIZE = 24, ITL_IWX_TXQ_V2_RESPONSE_SIZE = 8 };
enum itl_iwx_txq_v2_result {
    ITL_IWX_TXQ_V2_OK, ITL_IWX_TXQ_V2_INVALID, ITL_IWX_TXQ_V2_REJECTED
};
struct itl_iwx_txq_v2_response {
    uint16_t queue_number, write_pointer;
};

/* Serialization limits only: callers own hardware capability, DMA and IDs. */
static inline int
itl_iwx_txq_v2_parameters_valid(uint32_t station, uint32_t tid, uint32_t slots)
{
    return station <= 255 && tid <= 15 && slots >= 8 && slots <= 65536 &&
        (slots & (slots - 1)) == 0;
}

/* No output is modified on error; DMA addresses are serialized verbatim. */
static inline enum itl_iwx_txq_v2_result
itl_iwx_txq_v2_encode(void *out, size_t capacity, uint32_t station, uint32_t tid,
    uint32_t slots, uint64_t byte_count_addr, uint64_t descriptor_addr)
{
    uint8_t wire[ITL_IWX_TXQ_V2_REQUEST_SIZE] = {0};
    uint32_t exponent = 0;
    if (!out || capacity < sizeof(wire) ||
        !itl_iwx_txq_v2_parameters_valid(station, tid, slots))
        return ITL_IWX_TXQ_V2_INVALID;
    for (uint32_t n = slots; n > 8; n >>= 1)
        ++exponent;
    wire[0] = (uint8_t)station;
    wire[1] = (uint8_t)tid;
    wire[2] = 1; /* ENABLE_QUEUE, little endian uint16_t. */
    wire[4] = (uint8_t)exponent; /* log2(slots) - 3, LE uint32_t. */
    for (unsigned i = 0; i < 8; ++i) {
        wire[8 + i] = (uint8_t)(byte_count_addr >> (8 * i));
        wire[16 + i] = (uint8_t)(descriptor_addr >> (8 * i));
    }
    memcpy(out, wire, sizeof(wire));
    return ITL_IWX_TXQ_V2_OK;
}

/* out must not overlap bytes. Caller validates queue ownership/index limits. */
static inline enum itl_iwx_txq_v2_result
itl_iwx_txq_v2_decode(const void *bytes, size_t length,
    struct itl_iwx_txq_v2_response *out)
{
    if (!out)
        return ITL_IWX_TXQ_V2_INVALID;
    memset(out, 0, sizeof(*out));
    if (!bytes || length != ITL_IWX_TXQ_V2_RESPONSE_SIZE)
        return ITL_IWX_TXQ_V2_INVALID;
    const uint8_t *p = (const uint8_t *)bytes;
    if (p[2] || p[3]) /* Firmware contract: flags are set on failure. */
        return ITL_IWX_TXQ_V2_REJECTED;
    out->queue_number = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    out->write_pointer = (uint16_t)(p[4] | ((uint16_t)p[5] << 8));
    return ITL_IWX_TXQ_V2_OK;
}
#endif
