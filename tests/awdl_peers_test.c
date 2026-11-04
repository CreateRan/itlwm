/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itl80211/openbsd/net80211/awdl_peers.h"
/* Same independently encoded sync layout as the sync suite, invented MACs. */
static const uint8_t complete[] = {
  0xd0,0,0,0, 0xff,0xff,0xff,0xff,0xff,0xff,
  2,0x11,0x22,0x33,0x44,1, 0,0x25,0,0xff,0x94,0x73, 0x10,0,
  0x7f,0,0x17,0xf2,8,0x10,3,0, 0,0,0,0,0,0,0,0,
  4,0x49,0,
  6,0x20,0,149,2,0x10,0,0x6e,0,0,0,0x10,0,0x10,0,0,0,
  3,3,3,3,2,0xaa,0xbb,0xcc,0xdd,0xee,4,0,0xfe,0xff,0xfe,0xff,
  15,3,0,3,0xff,0xff,
  149,0x80,6,0x51,0,0,44,0x80,149,0x80,149,0x80,149,0x80,149,0x80,
  149,0x80,149,0x80,149,0x80,149,0x80,149,0x80,149,0x80,149,0x80,6,0x51,0,0
};
static const uint8_t self_mac[6] = {2,0x11,0x22,0x33,0x44,0xfe};
static const uint8_t peer1[6] = {2,0x11,0x22,0x33,0x44,1};
static const uint8_t peer2[6] = {2,0x11,0x22,0x33,0x44,2};
struct bytes { const uint8_t *data; size_t length; };
static int read_bytes(void *ctx, size_t offset, void *out, size_t length)
{
    struct bytes *b = (struct bytes *)ctx;
    assert(offset <= b->length && length <= b->length - offset);
    memcpy(out,b->data+offset,length); return 0;
}
static enum itl_awdl_peer_result observe(struct itl_awdl_peers *table,
    uint8_t peer, uint16_t sequence, uint64_t now, uint64_t ttl, int kind)
{
    uint8_t frame[sizeof(complete)]; memcpy(frame,complete,sizeof(frame));
    frame[15]=peer; frame[22]=(uint8_t)(sequence<<4); frame[23]=(uint8_t)(sequence>>4);
    size_t length = kind == 0 ? 40 : (kind == 2 ? 76 : sizeof(frame));
    if (kind == 2) frame[41]=33;
    if (kind == 3) {
        memmove(frame+43,frame+76,38); // Standalone sequence header + entries.
        frame[40]=18; frame[41]=41; frame[42]=0;
        memset(frame+81,0,3); length=84;
    }
    struct bytes b={frame,length}; struct itl_awdl_reader r={&b,length,read_bytes};
    return itl_awdl_peers_observe(table,&r,now,ttl);
}
static void test_bounded_table_and_explicit_selection(void)
{
    struct itl_awdl_peers table; struct itl_awdl_peer_window out;
    assert(itl_awdl_peers_init(&table,self_mac)==ITL_AWDL_PEER_OK);
    for (unsigned i=1;i<=16;++i) assert(observe(&table,(uint8_t)i,1,100,1000,1)==ITL_AWDL_PEER_OK);
    assert(observe(&table,17,1,101,1000,1)==ITL_AWDL_PEER_FULL);
    assert(itl_awdl_peers_find(&table,peer1)>=0);
    assert(itl_awdl_peers_project(&table,101,1000,1000,&out)==ITL_AWDL_PEER_MISSING);
    assert(itl_awdl_peers_select(&table,peer1,101,1000,1000)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,102,1000,1000,&out)==ITL_AWDL_PEER_OK);
    assert(memcmp(out.address,peer1,6)==0 && out.observation_us==100 && out.age_us==2);
    assert(out.window.aw_counter==65534 && out.window.channel==6);
    assert(itl_awdl_peers_forget(&table,peer1)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,103,1000,1000,&out)==ITL_AWDL_PEER_MISSING);
    assert(observe(&table,17,1,104,1000,1)==ITL_AWDL_PEER_OK);
    assert(!table.has_reference); // Reusing a slot never inherits selection.
}
static void test_duplicate_old_half_range_and_wrap(void)
{
    struct itl_awdl_peers table;
    assert(itl_awdl_peers_init(&table,self_mac)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,4095,100,1000,1)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,0,110,1000,1)==ITL_AWDL_PEER_OK);
    int slot=itl_awdl_peers_find(&table,peer1); struct itl_awdl_peer saved=table.entries[slot];
    assert(observe(&table,1,0,120,1000,1)==ITL_AWDL_PEER_DUPLICATE);
    assert(observe(&table,1,4095,130,1000,1)==ITL_AWDL_PEER_OLD);
    assert(observe(&table,1,2048,140,1000,1)==ITL_AWDL_PEER_OLD);
    assert(memcmp(&saved,&table.entries[slot],sizeof(saved))==0);
    assert(observe(&table,1,2047,150,1000,1)==ITL_AWDL_PEER_OK);
    assert(table.entries[slot].last_sequence==2047);
}
static void test_sync_age_does_not_follow_unrelated_frames(void)
{
    struct itl_awdl_peers table; struct itl_awdl_peer_window out, zero={0};
    assert(itl_awdl_peers_init(&table,self_mac)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,1,100,1000,1)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_select(&table,peer1,100,1000,100)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,2,150,1000,0)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,160,1000,59,&out)==ITL_AWDL_PEER_STALE);
    assert(memcmp(&out,&zero,sizeof(out))==0);
    assert(itl_awdl_peers_project(&table,160,1000,60,&out)==ITL_AWDL_PEER_OK);
    assert(out.observation_us==100 && out.age_us==60);
    assert(observe(&table,1,3,170,1000,2)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,170,1000,1000,&out)==ITL_AWDL_PEER_NO_SYNC);
    assert(observe(&table,1,4,180,1000,1)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,180,1000,0,&out)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,5,190,1000,3)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,190,1000,1000,&out)==ITL_AWDL_PEER_NO_SYNC);
    assert(observe(&table,1,6,200,1000,1)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,200,1000,0,&out)==ITL_AWDL_PEER_OK && out.observation_us==200);
}
static void test_expiry_boundaries_clock_and_clear(void)
{
    struct itl_awdl_peers table; struct itl_awdl_peer_window out;
    assert(itl_awdl_peers_init(&table,self_mac)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,1,100,100,1)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_select(&table,peer1,200,100,100)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,200,100,100,&out)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,201,100,100,&out)==ITL_AWDL_PEER_MISSING);
    assert(itl_awdl_peers_find(&table,peer1)<0);
    assert(observe(&table,2,1,202,100,1)==ITL_AWDL_PEER_OK);
    assert(!table.has_reference);
    struct itl_awdl_peers before=table;
    assert(observe(&table,1,5,201,100,1)==ITL_AWDL_PEER_CLOCK_REVERSED);
    assert(memcmp(&table,&before,sizeof(table))==0);
    assert(itl_awdl_peers_select(&table,peer2,202,100,100)==ITL_AWDL_PEER_OK);
    itl_awdl_peers_clear(&table);
    assert(!table.has_reference && itl_awdl_peers_find(&table,peer2)<0);
    assert(observe(&table,1,1,201,100,1)==ITL_AWDL_PEER_CLOCK_REVERSED);
    assert(observe(&table,1,1,202,100,1)==ITL_AWDL_PEER_OK);
}
static void test_invalid_frames_and_failed_selection_preserve_state(void)
{
    struct itl_awdl_peers table;
    assert(itl_awdl_peers_init(&table,self_mac)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,1,100,1000,1)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_select(&table,peer1,100,1000,1000)==ITL_AWDL_PEER_OK);
    struct itl_awdl_peers before=table;
    assert(observe(&table,0xfe,1,100,1000,1)==ITL_AWDL_PEER_INVALID);
    uint8_t bad[sizeof(complete)]; memcpy(bad,complete,sizeof(bad)); bad[10]=3;
    struct bytes b={bad,sizeof(bad)}; struct itl_awdl_reader r={&b,sizeof(bad),read_bytes};
    assert(itl_awdl_peers_observe(&table,&r,100,1000)==ITL_AWDL_PEER_INVALID);
    bad[10]=2; // Valid source for structural truncation and semantic failure.
    r.length=41;
    assert(itl_awdl_peers_observe(&table,&r,100,1000)==ITL_AWDL_PEER_INVALID);
    r.length=sizeof(bad); bad[48]=0; // Structurally valid TLV, invalid zero AW period.
    assert(itl_awdl_peers_observe(&table,&r,100,1000)==ITL_AWDL_PEER_INVALID);
    assert(memcmp(&before,&table,sizeof(table))==0);
    assert(itl_awdl_peers_select(&table,peer2,100,1000,1000)==ITL_AWDL_PEER_MISSING);
    assert(table.has_reference && memcmp(table.entries[table.reference_index].address,peer1,6)==0);
    assert(observe(&table,2,1,200,1000,0)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_select(&table,peer2,200,1000,1000)==ITL_AWDL_PEER_NO_SYNC);
    assert(memcmp(table.entries[table.reference_index].address,peer1,6)==0);
    struct itl_awdl_peer_window out;
    assert(itl_awdl_peers_project(&table,300,1000,199,&out)==ITL_AWDL_PEER_STALE);
    assert(itl_awdl_peers_init(NULL,self_mac)==ITL_AWDL_PEER_INVALID);
    assert(itl_awdl_peers_observe(&table,NULL,300,1000)==ITL_AWDL_PEER_INVALID);
}
static void test_rejected_ordering_still_expires_other_peers(void)
{
    struct itl_awdl_peers table;
    const uint8_t peer3[6]={2,0x11,0x22,0x33,0x44,3};
    assert(itl_awdl_peers_init(&table,self_mac)==ITL_AWDL_PEER_OK);
    assert(observe(&table,2,1,0,100,1)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,1,50,100,1)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_select(&table,peer2,50,100,100)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,1,101,100,1)==ITL_AWDL_PEER_DUPLICATE);
    assert(table.last_now_us==101 && !table.has_reference);
    assert(itl_awdl_peers_find(&table,peer2)<0);
    int slot=itl_awdl_peers_find(&table,peer1);
    assert(slot>=0 && table.entries[slot].last_seen_us==50);
    assert(observe(&table,3,1,102,100,1)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,2,150,100,1)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,1,203,100,1)==ITL_AWDL_PEER_OLD);
    assert(table.last_now_us==203 && itl_awdl_peers_find(&table,peer3)<0);
    assert(table.entries[slot].last_seen_us==150);
}
static void test_zero_ttl_and_max_clock(void)
{
    struct itl_awdl_peers table; struct itl_awdl_peer_window out;
    assert(itl_awdl_peers_init(&table,self_mac)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,1,0,0,1)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_select(&table,peer1,0,0,0)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,0,0,0,&out)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,1,0,0,&out)==ITL_AWDL_PEER_MISSING);
    assert(observe(&table,2,1,UINT64_MAX,UINT64_MAX,1)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_select(&table,peer2,UINT64_MAX,UINT64_MAX,0)==ITL_AWDL_PEER_OK);
    assert(itl_awdl_peers_project(&table,UINT64_MAX,UINT64_MAX,0,&out)==ITL_AWDL_PEER_OK && out.age_us==0);
}
static int fail_sequence_read(void *ctx, size_t offset, void *out, size_t length)
{
    if (offset==22) return -1;
    return read_bytes(ctx,offset,out,length);
}
static void test_invalid_arguments_and_late_reader_failure(void)
{
    struct itl_awdl_peers table; struct itl_awdl_peer_window out, zero={0};
    uint8_t invalid[6]={0};
    assert(itl_awdl_peers_init(&table,self_mac)==ITL_AWDL_PEER_OK);
    assert(observe(&table,1,1,10,100,1)==ITL_AWDL_PEER_OK);
    struct itl_awdl_peers before=table;
    assert(itl_awdl_peers_init(&table,NULL)==ITL_AWDL_PEER_INVALID);
    assert(itl_awdl_peers_init(&table,invalid)==ITL_AWDL_PEER_INVALID);
    assert(memcmp(&before,&table,sizeof(table))==0);
    struct bytes b={complete,sizeof(complete)};
    struct itl_awdl_reader reader={&b,sizeof(complete),fail_sequence_read};
    assert(itl_awdl_peers_observe(&table,&reader,20,100)==ITL_AWDL_PEER_INVALID);
    assert(memcmp(&before,&table,sizeof(table))==0);
    assert(itl_awdl_peers_expire(&table,9,100)==ITL_AWDL_PEER_CLOCK_REVERSED);
    assert(itl_awdl_peers_project(&table,9,100,100,&out)==ITL_AWDL_PEER_CLOCK_REVERSED);
    assert(memcmp(&out,&zero,sizeof(out))==0 && memcmp(&before,&table,sizeof(table))==0);
    assert(itl_awdl_peers_project(&table,100,100,100,NULL)==ITL_AWDL_PEER_INVALID);
    assert(memcmp(&before,&table,sizeof(table))==0);
    assert(itl_awdl_peers_project(NULL,100,100,100,&out)==ITL_AWDL_PEER_INVALID);
    assert(memcmp(&out,&zero,sizeof(out))==0);
}
static void test_randomized_state_invariants(void)
{
    struct itl_awdl_peers table; struct itl_awdl_peer_window out;
    assert(itl_awdl_peers_init(&table,self_mac)==ITL_AWDL_PEER_OK);
    uint32_t seed=0x12170455; uint64_t now=0;
    for (unsigned step=0;step<10000;++step) {
        seed=seed*1664525U+1013904223U;
        now+=(seed>>24)&15;
        uint8_t address[6]={2,0x11,0x22,0x33,0x44,(uint8_t)(1+(seed%20))};
        switch ((seed>>16)%5) {
            case 0: case 1:
                (void)observe(&table,address[5],(uint16_t)((seed>>4)&4095),now,100,(int)((seed>>20)%3));
                break;
            case 2: (void)itl_awdl_peers_select(&table,address,now,100,50); break;
            case 3: (void)itl_awdl_peers_forget(&table,address); break;
            case 4: (void)itl_awdl_peers_expire(&table,now,100); break;
        }
        enum itl_awdl_peer_result result=itl_awdl_peers_project(&table,now,100,50,&out);
        unsigned count=0;
        for (unsigned i=0;i<ITL_AWDL_PEER_LIMIT;++i) {
            if (!table.entries[i].used) continue;
            ++count;
            assert(table.entries[i].last_seen_us<=table.last_now_us);
            assert(now-table.entries[i].last_seen_us<=100);
            if (table.entries[i].has_sync)
                assert(table.entries[i].sync_received_us<=table.entries[i].last_seen_us);
            for (unsigned j=i+1;j<ITL_AWDL_PEER_LIMIT;++j)
                assert(!table.entries[j].used || memcmp(table.entries[i].address,table.entries[j].address,6)!=0);
        }
        assert(count<=16);
        if (table.has_reference) {
            assert(table.reference_index<16 && table.entries[table.reference_index].used);
            if (result==ITL_AWDL_PEER_OK) {
                assert(memcmp(out.address,table.entries[table.reference_index].address,6)==0);
                assert(out.age_us<=50 && out.window.to_next_aw_us>0);
            }
        } else assert(result==ITL_AWDL_PEER_MISSING);
    }
}
int main(void)
{
    test_bounded_table_and_explicit_selection(); test_duplicate_old_half_range_and_wrap();
    test_sync_age_does_not_follow_unrelated_frames(); test_expiry_boundaries_clock_and_clear();
    test_invalid_frames_and_failed_selection_preserve_state(); test_zero_ttl_and_max_clock();
    test_rejected_ordering_still_expires_other_peers();
    test_invalid_arguments_and_late_reader_failure();
    test_randomized_state_invariants();
    puts("AWDL peer table tests passed");
}
