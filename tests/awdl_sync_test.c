/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itl80211/openbsd/net80211/awdl_sync.h"
/* AWDL MIF, sync TLV value length 73: fixed33 + seq6+32 + padding2. */
static const uint8_t golden[] = {
  0xd0,0,0,0, 0xff,0xff,0xff,0xff,0xff,0xff,
  2,0x11,0x22,0x33,0x44,0x55, 0,0x25,0,0xff,0x94,0x73, 0x10,0,
  0x7f,0,0x17,0xf2,8,0x10,3,0, 0,0,0,0,0,0,0,0,
  4,0x49,0,
  6,0x20,0,149,2, 0x10,0,0x6e,0,0,0,0x10,0,0x10,0,0,0,
  3,3,3,3, 2,0xaa,0xbb,0xcc,0xdd,0xee, 4,0,0xfe,0xff,0xfe,0xff,
  15,3,0,3,0xff,0xff,
  149,0x80,6,0x51,0,0,44,0x80,149,0x80,149,0x80,149,0x80,149,0x80,
  149,0x80,149,0x80,149,0x80,149,0x80,149,0x80,149,0x80,149,0x80,6,0x51,
  0,0
};
static const uint8_t standalone[] = {
  18,41,0, 15,3,0,3,0xff,0xff,
  149,0x80,6,0x51,0,0,44,0x80,149,0x80,149,0x80,149,0x80,149,0x80,
  149,0x80,149,0x80,149,0x80,149,0x80,149,0x80,149,0x80,149,0x80,6,0x51,
  0,0,0
};
struct bytes { const uint8_t *a, *b; size_t split, length, fail_at; };
static int read_bytes(void *ctx, size_t offset, void *out, size_t length)
{
    struct bytes *b = ctx;
    assert(offset <= b->length && length <= b->length - offset);
    if (offset == b->fail_at) return -1;
    for (size_t i = 0; i < length; ++i) {
        size_t p = offset + i;
        ((uint8_t *)out)[i] = p < b->split ? b->a[p] : b->b[p - b->split];
    }
    return 0;
}
static enum itl_awdl_result parse(const uint8_t *data, size_t length, struct itl_awdl_sync *out)
{
    struct bytes b = {data, data + length, length, length, (size_t)-1};
    struct itl_awdl_reader r = {&b, length, read_bytes};
    return itl_awdl_parse_sync(&r, out);
}
static void test_golden_and_all_splits(void)
{
    struct itl_awdl_sync out;
    assert(sizeof(golden) == 116);
    assert(parse(golden, sizeof(golden), &out) == ITL_AWDL_VALID);
    assert(out.has_sync && out.has_sequence && out.presence_mode == 4);
    assert(out.aw_period_tu == 16 && out.af_period_tu == 110);
    assert(out.tx_down_counter_tu == 32 && out.aw_counter == 65534);
    assert(out.next_channel == 6 && out.master_channel == 149 && out.guard_time == 2);
    assert(memcmp(out.master, "\x02\xaa\xbb\xcc\xdd\xee", 6) == 0);
    assert(out.sequence.encoding == 3 && out.sequence.channel[0] == 149);
    assert(out.sequence.channel[1] == 6 && out.sequence.auxiliary[1] == 0x51);
    assert(out.sequence.channel[2] == 0 && out.sequence.channel[15] == 6);
    uint8_t first[117], second[117];
    for (size_t split = 0; split <= sizeof(golden); ++split) {
        memcpy(first + 1, golden, split);
        memcpy(second + 1, golden + split, sizeof(golden) - split);
        struct bytes b = {first + 1, second + 1, split, sizeof(golden), (size_t)-1};
        struct itl_awdl_reader r = {&b, sizeof(golden), read_bytes};
        assert(itl_awdl_parse_sync(&r, &out) == ITL_AWDL_VALID && out.aw_counter == 65534);
    }
}
static void test_truncation_and_rejection(void)
{
    struct itl_awdl_sync out, zero = {0};
    for (size_t n = 0; n < sizeof(golden); ++n) {
        enum itl_awdl_result result = parse(golden, n, &out);
        if (n == 40) assert(result == ITL_AWDL_VALID && !out.has_sync && !out.has_sequence);
        else { assert(result == ITL_AWDL_TRUNCATED); assert(memcmp(&out, &zero, sizeof(out)) == 0); }
    }
    const struct { size_t p; uint8_t value; enum itl_awdl_result want; } cases[] = {
      {48,0,ITL_AWDL_MALFORMED}, {44,65,ITL_AWDL_MALFORMED},
      {70,2,ITL_AWDL_UNSUPPORTED}, {76,14,ITL_AWDL_UNSUPPORTED},
      {77,2,ITL_AWDL_UNSUPPORTED}, {78,1,ITL_AWDL_UNSUPPORTED},
      {79,1,ITL_AWDL_UNSUPPORTED}, {80,0,ITL_AWDL_UNSUPPORTED},
      {114,1,ITL_AWDL_MALFORMED}
    };
    uint8_t b[sizeof(golden)];
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        memcpy(b, golden, sizeof(b)); b[cases[i].p] = cases[i].value;
        assert(parse(b, sizeof(b), &out) == cases[i].want);
        assert(memcmp(&out, &zero, sizeof(out)) == 0);
    }
}
static void test_tlv_sources_and_conflicts(void)
{
    uint8_t b[256]; struct itl_awdl_sync out;
    memcpy(b, golden, sizeof(golden));
    memcpy(b + sizeof(golden), standalone, sizeof(standalone));
    assert(parse(b, sizeof(golden) + sizeof(standalone), &out) == ITL_AWDL_VALID);
    b[sizeof(golden) + 9] = 6; // Contradictory standalone channel 0.
    assert(parse(b, sizeof(golden) + sizeof(standalone), &out) == ITL_AWDL_MALFORMED);
    memcpy(b + sizeof(golden), golden + 40, sizeof(golden) - 40);
    assert(parse(b, sizeof(golden) * 2 - 40, &out) == ITL_AWDL_MALFORMED);
    // Fixed sync TLV followed by a separate sequence TLV.
    memcpy(b, golden, 76); b[41] = 33;
    memcpy(b + 76, standalone, sizeof(standalone));
    assert(parse(b, 76 + sizeof(standalone), &out) == ITL_AWDL_VALID && out.has_sequence);
    assert(parse(b, 76, &out) == ITL_AWDL_VALID && out.has_sync && !out.has_sequence);
    memcpy(b, golden, 40); memcpy(b + 40, standalone, sizeof(standalone));
    assert(parse(b, 40 + sizeof(standalone), &out) == ITL_AWDL_VALID && !out.has_sync && out.has_sequence);
}
static void test_encodings(void)
{
    uint8_t b[116]; struct itl_awdl_sync out;
    memcpy(b, golden, sizeof(b)); b[77] = 1;
    for (unsigned i = 0; i < 16; ++i) { b[82 + 2*i] = 0x20; b[83 + 2*i] = 44; }
    assert(parse(b, sizeof(b), &out) == ITL_AWDL_VALID);
    assert(out.sequence.encoding == 1 && out.sequence.channel[3] == 44 && out.sequence.auxiliary[3] == 0x20);
    memcpy(b, golden, 82); b[41] = 57; b[77] = 0;
    const uint8_t channels[16] = {149,6,0,44,149,149,149,149,149,149,149,149,149,149,149,6};
    memcpy(b + 82, channels, 16); b[98] = b[99] = 0;
    assert(parse(b, 100, &out) == ITL_AWDL_VALID);
    assert(out.sequence.encoding == 0 && out.sequence.channel[3] == 44 && out.sequence.auxiliary[3] == 0);
}
static void test_reader_failures_unknown_tlvs_and_ht_control(void)
{
    struct itl_awdl_sync out, zero = {0};
    struct bytes b = {golden, golden + sizeof(golden), sizeof(golden), sizeof(golden), 43};
    struct itl_awdl_reader reader = {&b, sizeof(golden), read_bytes};
    assert(itl_awdl_parse_sync(&reader, &out) == ITL_AWDL_TRUNCATED);
    assert(memcmp(&out, &zero, sizeof(out)) == 0);
    b.fail_at = 82; // Fail reading first embedded channel after valid metadata.
    assert(itl_awdl_parse_sync(&reader, &out) == ITL_AWDL_TRUNCATED);
    assert(memcmp(&out, &zero, sizeof(out)) == 0);
    assert(itl_awdl_parse_sync(NULL, &out) == ITL_AWDL_MALFORMED);
    assert(itl_awdl_parse_sync(&reader, NULL) == ITL_AWDL_MALFORMED);
    reader.length = (size_t)-1;
    assert(itl_awdl_parse_sync(&reader, &out) == ITL_AWDL_MALFORMED);
    uint8_t bytes[160];
    memcpy(bytes, golden, sizeof(golden));
    bytes[116]=0xfe; bytes[117]=1; bytes[118]=0; bytes[119]=0x99;
    assert(parse(bytes,120,&out) == ITL_AWDL_VALID && out.has_sync);
    memcpy(bytes,golden,24); bytes[1]=0x80;
    memset(bytes+24,0,4); memcpy(bytes+28,golden+24,sizeof(golden)-24);
    assert(parse(bytes,120,&out) == ITL_AWDL_VALID && out.aw_counter == 65534);
    struct itl_awdl_channel_sequence sequence, sequence_zero = {0};
    memset(&sequence,0xa5,sizeof(sequence));
    assert(itl_awdl_parse_sequence(NULL,0,0,2,&sequence) == ITL_AWDL_MALFORMED);
    assert(memcmp(&sequence,&sequence_zero,sizeof(sequence)) == 0);
}
static void test_projection_boundaries_and_wrap(void)
{
    struct itl_awdl_sync sync; struct itl_awdl_window out;
    assert(parse(golden, sizeof(golden), &sync) == ITL_AWDL_VALID);
    const struct { uint64_t elapsed; uint16_t counter; uint8_t index, channel; uint32_t next; } cases[] = {
      {0,65534,15,6,16384}, {16383,65534,15,6,1},
      {16384,65535,15,6,16384}, {32767,65535,15,6,1},
      {32768,0,0,149,16384}, {49152,1,0,149,16384},
      {98304,4,1,6,16384}, {163840,8,2,0,16384}
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        assert(itl_awdl_project_window(&sync,1000000,1000000+cases[i].elapsed,200000,&out) == ITL_AWDL_VALID);
        assert(out.aw_counter == cases[i].counter && out.sequence_index == cases[i].index);
        assert(out.channel == cases[i].channel && out.to_next_aw_us == cases[i].next);
    }
    assert(itl_awdl_project_window(&sync,0,UINT64_MAX,UINT64_MAX,&out) == ITL_AWDL_VALID);
    assert(out.aw_counter == 65533 && out.to_next_aw_us == 1 && out.to_next_eaw_us == 32769);
    assert(itl_awdl_project_window(&sync,UINT64_MAX-10,UINT64_MAX,10,&out) == ITL_AWDL_VALID);
    assert(out.aw_counter == 65534 && out.to_next_aw_us == 16374);
    sync.tx_down_counter_tu = 0;
    assert(itl_awdl_project_window(&sync,100,100,0,&out) == ITL_AWDL_VALID && out.aw_counter == 0);
}
static void test_projection_failures_and_fuzz(void)
{
    struct itl_awdl_sync sync; struct itl_awdl_window out, zero = {0};
    assert(parse(golden, sizeof(golden), &sync) == ITL_AWDL_VALID);
    assert(itl_awdl_project_window(&sync,100,99,100,&out) == ITL_AWDL_MALFORMED);
    assert(itl_awdl_project_window(&sync,100,201,100,&out) == ITL_AWDL_UNSUPPORTED);
    assert(memcmp(&out,&zero,sizeof(out)) == 0);
    assert(itl_awdl_project_window(&sync,100,200,100,&out) == ITL_AWDL_VALID);
    sync.has_sequence = 0;
    assert(itl_awdl_project_window(&sync,100,100,100,&out) == ITL_AWDL_UNSUPPORTED);
    assert(itl_awdl_project_window(NULL,0,0,0,&out) == ITL_AWDL_MALFORMED);
    assert(itl_awdl_project_window(&sync,0,0,0,NULL) == ITL_AWDL_MALFORMED);
    uint8_t data[256]; uint32_t seed = 0x99127344;
    for (unsigned run = 0; run < 20000; ++run) {
        for (size_t i = 0; i < sizeof(data); ++i) { seed=seed*1664525U+1013904223U; data[i]=(uint8_t)(seed>>24); }
        size_t length=(seed>>8)%sizeof(data);
        if (run&1) { memcpy(data,golden,sizeof(golden)); data[(seed>>16)%sizeof(golden)]^=(uint8_t)seed; }
        struct itl_awdl_sync parsed;
        if (parse(data,length,&parsed)==ITL_AWDL_VALID)
            (void)itl_awdl_project_window(&parsed,0,seed,UINT64_MAX,&out);
    }
}
int main(void)
{
    test_golden_and_all_splits(); test_truncation_and_rejection();
    test_tlv_sources_and_conflicts(); test_encodings();
    test_reader_failures_unknown_tlvs_and_ht_control();
    test_projection_boundaries_and_wrap(); test_projection_failures_and_fuzz();
    puts("AWDL sync parser and window tests passed");
}
