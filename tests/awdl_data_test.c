/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itl80211/openbsd/net80211/awdl_data.h"
/* Independent hand-encoded unencrypted, unfragmented AWDL frame.
 * Opaque payload is deliberately not a full IPv6 packet: this is a wire codec.
 */
static const uint8_t frame[] = {
    0x08,0x00,0,0, 0x33,0x33,0,0,0,0xfb,
    0x02,0x11,0x22,0x33,0x44,0x55, 0,0x25,0,0xff,0x94,0x73, 0x10,0,
    0xaa,0xaa,0x03,0,0x17,0xf2,0x08,0,
    0x03,0x04,0x34,0x12,0,0,0x86,0xdd,
    0x60,0,0,0
};
struct fragments {
    const uint8_t *a, *b;
    size_t split, length, fail_at;
};
static int read_fragments(void *context, size_t offset, void *out, size_t length)
{
    struct fragments *f = context;
    assert(offset <= f->length && length <= f->length - offset);
    if (offset == f->fail_at) return -1;
    for (size_t i = 0; i < length; ++i) {
        size_t p = offset + i;
        ((uint8_t *)out)[i] = p < f->split ? f->a[p] : f->b[p - f->split];
    }
    return 0;
}
static enum itl_awdl_result parse(const uint8_t *bytes, size_t length, struct itl_awdl_data *out)
{
    struct fragments f = {bytes, bytes + length, length, length, (size_t)-1};
    struct itl_awdl_reader reader = {&f, length, read_fragments};
    return itl_awdl_parse_data(&reader, out);
}
static void test_decode_golden_frame(void)
{
    struct itl_awdl_data out;
    assert(parse(frame, sizeof(frame), &out) == ITL_AWDL_VALID);
    assert(out.sequence == 0x1234 && out.ether_type == 0x86dd);
    assert(out.payload_offset == 40 && out.payload_length == 4);
    assert(memcmp(out.source, "\x02\x11\x22\x33\x44\x55", 6) == 0);
    assert(memcmp(out.destination, "\x33\x33\x00\x00\x00\xfb", 6) == 0);
    for (size_t length = 0; length < 40; ++length) {
        memset(&out, 0xa5, sizeof(out));
        assert(parse(frame, length, &out) == ITL_AWDL_TRUNCATED);
        const struct itl_awdl_data zero = {0};
        assert(memcmp(&out, &zero, sizeof(out)) == 0);
    }
    assert(parse(frame, 40, &out) == ITL_AWDL_VALID && out.payload_length == 0);
}
static void test_encode_golden_header_and_bounds(void)
{
    const uint8_t expected[] = {0xaa,0xaa,3,0,0x17,0xf2,8,0,3,4,0x34,0x12,0,0,0x86,0xdd};
    uint8_t output[18], untouched[18];
    memset(untouched, 0xa5, sizeof(untouched));
    for (size_t capacity = 0; capacity < 16; ++capacity) {
        memcpy(output, untouched, sizeof(output));
        assert(itl_awdl_write_data_header(output + 1, capacity, 0x1234, 0x86dd) == ITL_AWDL_TRUNCATED);
        assert(memcmp(output, untouched, sizeof(output)) == 0);
    }
    memcpy(output, untouched, sizeof(output));
    assert(itl_awdl_write_data_header(output + 1, 16, 0x1234, 0x86dd) == ITL_AWDL_VALID);
    assert(output[0] == 0xa5 && output[17] == 0xa5);
    assert(memcmp(output + 1, expected, 16) == 0);
    assert(itl_awdl_write_data_header(output, 16, 0xffff, 0x0806) == ITL_AWDL_VALID);
    assert(output[10] == 0xff && output[11] == 0xff && output[14] == 8 && output[15] == 6);
    assert(itl_awdl_write_data_header(output, 16, 0, 0x86dd) == ITL_AWDL_VALID);
    assert(output[10] == 0 && output[11] == 0);
    memcpy(output, untouched, sizeof(output));
    assert(itl_awdl_write_data_header(output, 16, 1, 1500) == ITL_AWDL_MALFORMED);
    assert(memcmp(output, untouched, sizeof(output)) == 0);
    assert(itl_awdl_write_data_header(NULL, 16, 1, 0x86dd) == ITL_AWDL_MALFORMED);
    assert(itl_awdl_write_data_header(output, 16, 1, 0x05ff) == ITL_AWDL_MALFORMED);
    assert(itl_awdl_write_data_header(output, 16, 1, 0x0600) == ITL_AWDL_VALID);
    assert(output[14] == 6 && output[15] == 0);
}
static void test_wrong_and_unsupported_frames(void)
{
    uint8_t bytes[sizeof(frame)];
    struct itl_awdl_data out, zero = {0};
    const struct { size_t position; uint8_t byte; enum itl_awdl_result want; } cases[] = {
        {0,0xd0,ITL_AWDL_NOT_AWDL}, {0,0x48,ITL_AWDL_NOT_AWDL},
        {0,0x09,ITL_AWDL_NOT_AWDL}, {16,1,ITL_AWDL_NOT_AWDL},
        {24,0xab,ITL_AWDL_NOT_AWDL}, {29,0xf1,ITL_AWDL_NOT_AWDL},
        {30,0x86,ITL_AWDL_NOT_AWDL}, {32,2,ITL_AWDL_UNSUPPORTED},
        {36,1,ITL_AWDL_UNSUPPORTED}, {38,0,ITL_AWDL_MALFORMED},
        {1,1,ITL_AWDL_UNSUPPORTED}, {1,0x40,ITL_AWDL_UNSUPPORTED},
        {1,4,ITL_AWDL_UNSUPPORTED}, {22,0x11,ITL_AWDL_UNSUPPORTED}
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        memcpy(bytes, frame, sizeof(frame)); bytes[cases[i].position] = cases[i].byte;
        assert(parse(bytes, sizeof(bytes), &out) == cases[i].want);
        assert(memcmp(&out, &zero, sizeof(out)) == 0);
    }
}
static void test_qos_ht_and_all_segment_splits(void)
{
    uint8_t qos[sizeof(frame) + 6], first[sizeof(qos) + 1], second[sizeof(qos) + 1];
    memcpy(qos, frame, 24); qos[0] = 0x88; qos[1] = 0x80;
    memset(qos + 24, 0, 6); qos[24] = 3; /* TID 3, then HT Control. */
    memcpy(qos + 30, frame + 24, sizeof(frame) - 24);
    struct itl_awdl_data out;
    assert(parse(qos, sizeof(qos), &out) == ITL_AWDL_VALID);
    assert(out.payload_offset == 46 && out.payload_length == 4);
    for (size_t length = 0; length < 46; ++length)
        assert(parse(qos, length, &out) == ITL_AWDL_TRUNCATED);
    for (size_t split = 0; split <= sizeof(qos); ++split) {
        memcpy(first + 1, qos, split); memcpy(second + 1, qos + split, sizeof(qos) - split);
        struct fragments f = {first + 1, second + 1, split, sizeof(qos), (size_t)-1};
        struct itl_awdl_reader reader = {&f, sizeof(qos), read_fragments};
        assert(itl_awdl_parse_data(&reader, &out) == ITL_AWDL_VALID);
        assert(out.sequence == 0x1234 && out.payload_offset == 46);
    }
    qos[24] |= 0x80; /* A-MSDU aggregation needs a different decoder. */
    assert(parse(qos, sizeof(qos), &out) == ITL_AWDL_UNSUPPORTED);
    qos[24] = 3; qos[1] = 0;
    memmove(qos + 26, qos + 30, sizeof(frame) - 24);
    assert(parse(qos, sizeof(frame) + 2, &out) == ITL_AWDL_VALID && out.payload_offset == 42);
}
static void test_frame_length_limit(void)
{
    uint8_t bytes[4097] = {0};
    memcpy(bytes, frame, sizeof(frame));
    struct itl_awdl_data out;
    assert(parse(bytes, 4096, &out) == ITL_AWDL_VALID);
    assert(out.payload_offset == 40 && out.payload_length == 4056);
    assert(parse(bytes, 4097, &out) == ITL_AWDL_MALFORMED);
}
static void test_failed_reader_and_fuzz(void)
{
    struct itl_awdl_data out, zero = {0};
    struct fragments f = {frame, frame + sizeof(frame), sizeof(frame), sizeof(frame), 24};
    struct itl_awdl_reader reader = {&f, sizeof(frame), read_fragments};
    assert(itl_awdl_parse_data(&reader, &out) == ITL_AWDL_TRUNCATED);
    assert(memcmp(&out, &zero, sizeof(out)) == 0);
    assert(itl_awdl_parse_data(NULL, &out) == ITL_AWDL_MALFORMED);
    assert(itl_awdl_parse_data(&reader, NULL) == ITL_AWDL_MALFORMED);
    reader.length = (size_t)-1;
    assert(itl_awdl_parse_data(&reader, &out) == ITL_AWDL_MALFORMED);
    reader.length = sizeof(frame); reader.read = NULL;
    assert(itl_awdl_parse_data(&reader, &out) == ITL_AWDL_MALFORMED);
    uint8_t bytes[128]; uint32_t seed = 0x3910cafe;
    for (unsigned run = 0; run < 20000; ++run) {
        for (size_t i = 0; i < sizeof(bytes); ++i) {
            seed = seed * 1664525U + 1013904223U; bytes[i] = (uint8_t)(seed >> 24);
        }
        size_t length = (seed >> 8) % sizeof(bytes);
        if (run & 1) {
            memcpy(bytes, frame, sizeof(frame)); bytes[(seed >> 16) % sizeof(frame)] ^= (uint8_t)seed;
        }
        (void)parse(bytes, length, &out);
    }
}
int main(void)
{
    test_decode_golden_frame();
    test_encode_golden_header_and_bounds();
    test_wrong_and_unsupported_frames();
    test_qos_ht_and_all_segment_splits();
    test_frame_length_limit();
    test_failed_reader_and_fuzz();
    puts("AWDL data codec tests passed");
}
