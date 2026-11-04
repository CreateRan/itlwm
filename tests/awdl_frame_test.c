/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itl80211/openbsd/net80211/awdl_frame.h"

/* Hand-encoded from the AWDL PSF/MIF wire format; no production builder. */
static const uint8_t mif[] = {
    0xd0,0x00,0x00,0x00, 0xff,0xff,0xff,0xff,0xff,0xff,
    0x02,0x11,0x22,0x33,0x44,0x55, 0x00,0x25,0x00,0xff,0x94,0x73,
    0x10,0x00,
    0x7f,0x00,0x17,0xf2,0x08,0x10,0x03,0x00,
    0x78,0x56,0x34,0x12,0x01,0x00,0x00,0x80,
    0x15,0x02,0x00,0x31,0x02, /* version TLV, 2-byte value */
    0xfe,0x00,0x00 /* unknown zero-length TLV */
};
struct bytes { const uint8_t *data; size_t length; int fail; };
static int read_bytes(void *context, size_t offset, void *out, size_t length)
{
    struct bytes *b = context;
    assert(offset <= b->length && length <= b->length - offset);
    if (b->fail) return -1;
    /* Byte-by-byte reads also exercise unaligned frame starts. */
    for (size_t i = 0; i < length; ++i)
        ((uint8_t *)out)[i] = b->data[offset + i];
    return 0;
}
static enum itl_awdl_result parse(const uint8_t *data, size_t length,
                                  struct itl_awdl_action *out)
{
    struct bytes b = {data, length, 0};
    struct itl_awdl_reader r = {&b, length, read_bytes};
    return itl_awdl_parse_action(&r, out);
}
static void test_mif(void)
{
    struct itl_awdl_action out;
    assert(parse(mif, sizeof(mif), &out) == ITL_AWDL_VALID);
    assert(out.subtype == 3 && out.version == 0x10);
    assert(out.phy_tx == 0x12345678 && out.target_tx == 0x80000001);
    assert(memcmp(out.source, "\x02\x11\x22\x33\x44\x55", 6) == 0);
    assert(memcmp(out.destination, "\xff\xff\xff\xff\xff\xff", 6) == 0);
    assert(out.tlv_count == 2 && out.tlv_types == (1U << 21));
}
static void test_truncation_and_tlvs(void)
{
    struct itl_awdl_action out, zero = {0};
    for (size_t n = 0; n < sizeof(mif); ++n) {
        memset(&out, 0xa5, sizeof(out));
        enum itl_awdl_result result = parse(mif, n, &out);
        /* Header-only action and the first complete TLV are valid prefixes. */
        if (n == 40 || n == 45) assert(result == ITL_AWDL_VALID);
        else {
            assert(result != ITL_AWDL_VALID);
            assert(memcmp(&out, &zero, sizeof(out)) == 0);
        }
    }
    uint8_t broken[sizeof(mif)];
    memcpy(broken, mif, sizeof(mif));
    broken[41] = 0xff; broken[42] = 0xff;
    assert(parse(broken, sizeof(broken), &out) == ITL_AWDL_TRUNCATED);
}
static void test_other_frames(void)
{
    struct itl_awdl_action out;
    uint8_t frame[sizeof(mif) + 4];
    const struct { size_t offset; uint8_t value; enum itl_awdl_result want; } cases[] = {
        {0, 0x80, ITL_AWDL_NOT_AWDL}, /* beacon */
        {0, 0xd1, ITL_AWDL_NOT_AWDL}, /* wrong 802.11 version */
        {24, 4, ITL_AWDL_NOT_AWDL}, /* public action */
        {27, 0xf1, ITL_AWDL_NOT_AWDL}, /* other vendor */
        {28, 9, ITL_AWDL_NOT_AWDL}, /* other Apple protocol */
        {29, 0x20, ITL_AWDL_UNSUPPORTED}, /* newer AWDL */
        {30, 1, ITL_AWDL_UNSUPPORTED}, /* unknown subtype */
        {1, 0x40, ITL_AWDL_UNSUPPORTED}, /* encrypted */
        {1, 0x04, ITL_AWDL_UNSUPPORTED}, /* more fragments */
        {22, 0x11, ITL_AWDL_UNSUPPORTED}, /* non-first fragment */
        {1, 0x01, ITL_AWDL_MALFORMED} /* management ToDS */
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        memcpy(frame, mif, sizeof(mif));
        frame[cases[i].offset] = cases[i].value;
        assert(parse(frame, sizeof(mif), &out) == cases[i].want);
    }
    memcpy(frame, mif, 24);
    frame[1] = 0x80; /* HT Control extends the management header by four. */
    memset(frame + 24, 0, 4);
    memcpy(frame + 28, mif + 24, sizeof(mif) - 24);
    assert(parse(frame, sizeof(frame), &out) == ITL_AWDL_VALID);
    frame[34] = 0; /* PSF */
    assert(parse(frame, sizeof(frame), &out) == ITL_AWDL_VALID && out.subtype == 0);
}
static void test_reader_failure(void)
{
    struct itl_awdl_action out, zero = {0};
    struct bytes b = {mif, sizeof(mif), 1};
    struct itl_awdl_reader r = {&b, sizeof(mif), read_bytes};
    assert(itl_awdl_parse_action(&r, &out) == ITL_AWDL_TRUNCATED);
    assert(memcmp(&out, &zero, sizeof(out)) == 0);
    assert(itl_awdl_parse_action(NULL, &out) == ITL_AWDL_MALFORMED);
    assert(itl_awdl_parse_action(&r, NULL) == ITL_AWDL_MALFORMED);
    r.read = NULL;
    assert(itl_awdl_parse_action(&r, &out) == ITL_AWDL_MALFORMED);
}
/* Cross-segment reads use the same callback contract as mbuf_copydata. */
struct segments { const uint8_t *first, *second; size_t split, length; };
static int read_segments(void *context, size_t offset, void *out, size_t length)
{
    struct segments *b = context;
    assert(offset <= b->length && length <= b->length - offset);
    for (size_t i = 0; i < length; ++i) {
        size_t position = offset + i;
        ((uint8_t *)out)[i] = position < b->split ? b->first[position] :
                              b->second[position - b->split];
    }
    return 0;
}
static void test_segmented_unaligned_frames(void)
{
    uint8_t first[sizeof(mif) + 1], second[sizeof(mif) + 1];
    struct itl_awdl_action out;
    for (size_t split = 0; split <= sizeof(mif); ++split) {
        memcpy(first + 1, mif, split);
        memcpy(second + 1, mif + split, sizeof(mif) - split);
        struct segments b = {first + 1, second + 1, split, sizeof(mif)};
        struct itl_awdl_reader reader = {&b, sizeof(mif), read_segments};
        assert(itl_awdl_parse_action(&reader, &out) == ITL_AWDL_VALID);
        assert(out.phy_tx == 0x12345678 && out.tlv_count == 2);
    }
}
static void test_oversized_and_mutated_frames(void)
{
    struct itl_awdl_action out;
    struct bytes b = {mif, sizeof(mif), 0};
    struct itl_awdl_reader reader = {&b, (size_t)-1, read_bytes};
    assert(itl_awdl_parse_action(&reader, &out) == ITL_AWDL_MALFORMED);
    uint8_t data[128];
    uint32_t state = 0x12345678;
    for (unsigned run = 0; run < 20000; ++run) {
        for (size_t i = 0; i < sizeof(data); ++i) {
            state = state * 1664525U + 1013904223U;
            data[i] = (uint8_t)(state >> 24);
        }
        size_t length = (state >> 8) % sizeof(data);
        if (run & 1) {
            memcpy(data, mif, sizeof(mif));
            data[(state >> 16) % sizeof(mif)] ^= (uint8_t)state;
        }
        /* ASan/UBSan and the reader's bounds assertion catch unsafe reads. */
        (void)parse(data, length, &out);
    }
}
int main(void)
{
    test_mif();
    test_truncation_and_tlvs();
    test_other_frames();
    test_reader_failure();
    test_segmented_unaligned_frames();
    test_oversized_and_mutated_frames();
    puts("AWDL frame tests passed");
    return 0;
}
