/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itlwm/hal_iwx/iwx_flush_v1.h"
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void test_management_tid_does_not_index_data_array(void)
{
    const int32_t data[8]={10,11,-1,13,14,15,16,17};
    assert(itl_iwx_flush_binding_matches(15,3,data,8,3));
    assert(!itl_iwx_flush_binding_matches(15,10,data,8,3));
    assert(!itl_iwx_flush_binding_matches(15,3,data,8,-1));
    assert(itl_iwx_flush_binding_matches(7,17,data,8,3));
    assert(!itl_iwx_flush_binding_matches(7,10,data,8,3));
    assert(!itl_iwx_flush_binding_matches(2,65535,data,8,3));
    assert(!itl_iwx_flush_binding_matches(8,17,data,8,3));
    assert(!itl_iwx_flush_binding_matches(65535,17,data,8,3));
    assert(!itl_iwx_flush_binding_matches(0,10,NULL,8,3));
    assert(!itl_iwx_flush_binding_matches(0,10,data,0,3));
    /* Zero means no data binding in the TX path, not ownership of command Q0. */
    const int32_t unassigned[8]={0};
    assert(!itl_iwx_flush_binding_matches(0,0,unassigned,8,3));
    assert(!itl_iwx_flush_binding_matches(15,0,data,8,0));
}
static void test_literal_reply_and_unaligned_input(void)
{
    const uint8_t fixture[132]={3,0,2,0,
        0,0,10,0,0x34,0x12,0x78,0x56,
        15,0,3,0,0xfe,0xff,0xff,0xff};
    uint8_t unaligned[133]; memcpy(unaligned+1,fixture,132);
    struct itl_iwx_flush_response out;
    assert(itl_iwx_flush_decode(unaligned+1,132,3,0x8001,&out)==ITL_IWX_FLUSH_OK);
    assert(out.count==2);
    assert(out.entries[0].tid==0 && out.entries[0].queue==10);
    assert(out.entries[0].read_before==0x1234 && out.entries[0].read_after==0x5678);
    assert(out.entries[1].tid==15 && out.entries[1].queue==3);
    assert(out.entries[1].read_before==65534 && out.entries[1].read_after==65535);
    for (unsigned i=2;i<16;++i) assert(out.entries[i].tid==0 && out.entries[i].queue==0);
}
static void invalid(const uint8_t *bytes, size_t length, uint32_t station, uint16_t tids)
{
    struct itl_iwx_flush_response out,zero; memset(&out,0xa5,sizeof(out)); memset(&zero,0,sizeof(zero));
    assert(itl_iwx_flush_decode(bytes,length,station,tids,&out)==ITL_IWX_FLUSH_INVALID);
    assert(memcmp(&out,&zero,sizeof(out))==0);
}
static void test_lengths_station_counts_and_empty_evidence(void)
{
    uint8_t reply[133]={0}; struct itl_iwx_flush_response out,zero; memset(&zero,0,sizeof(zero));
    for (size_t length=1;length<132;++length) invalid(reply,length,0,0xffff);
    invalid(reply,133,0,0xffff); invalid(NULL,132,0,0xffff);
    invalid(reply,132,65536,0xffff); invalid(reply,132,UINT32_MAX,0xffff);
    assert(itl_iwx_flush_decode(reply,132,0,0xffff,NULL)==ITL_IWX_FLUSH_INVALID);
    memset(&out,0xa5,sizeof(out));
    assert(itl_iwx_flush_decode(NULL,0,0,0xffff,&out)==ITL_IWX_FLUSH_NO_REPLY);
    assert(memcmp(&out,&zero,sizeof(out))==0);
    /* A full zero-count reply is valid, and still contains no queue evidence. */
    assert(itl_iwx_flush_decode(reply,132,0,0xffff,&out)==ITL_IWX_FLUSH_OK && out.count==0);
    reply[0]=1; invalid(reply,132,0,0xffff); reply[0]=0;
    reply[2]=17; invalid(reply,132,0,0xffff);
    reply[2]=0; reply[3]=1; invalid(reply,132,0,0xffff);
}
static void test_full_count_and_invalid_tail_are_atomic(void)
{
    uint8_t reply[132]={0}; reply[2]=16;
    for (unsigned i=0;i<16;++i) {
        put16(reply+4+8*i,(uint16_t)i); put16(reply+6+8*i,(uint16_t)(496+i));
        put16(reply+8+8*i,65535); put16(reply+10+8*i,(uint16_t)(65535-i));
    }
    struct itl_iwx_flush_response out;
    assert(itl_iwx_flush_decode(reply,132,0,0xffff,&out)==ITL_IWX_FLUSH_OK && out.count==16);
    assert(out.entries[15].queue==511 && out.entries[15].read_after==65520);
    put16(reply+4+15*8,16); invalid(reply,132,0,0xffff);
    put16(reply+4+15*8,65535); invalid(reply,132,0,0xffff);
    put16(reply+4+15*8,15); invalid(reply,132,0,0x7fff);
    put16(reply+6+15*8,512); invalid(reply,132,0,0xffff);
    put16(reply+6+15*8,65535); invalid(reply,132,0,0xffff);
    put16(reply+6+15*8,496); invalid(reply,132,0,0xffff); /* Duplicate queue, not twice reclaimed. */
    put16(reply+6+15*8,511);
    assert(itl_iwx_flush_decode(reply,132,0,0xffff,&out)==ITL_IWX_FLUSH_OK);
    /* Unused fixed-array bytes are opaque, not additional entries. */
    reply[2]=1; memset(reply+12,0xff,120);
    assert(itl_iwx_flush_decode(reply,132,0,1,&out)==ITL_IWX_FLUSH_OK && out.count==1);
}
static void test_mutated_replies(void)
{
    uint32_t random=0x58ac34d1;
    uint8_t reply[133]; struct itl_iwx_flush_response out;
    for (unsigned iteration=0;iteration<20000;++iteration) {
        for (unsigned i=0;i<sizeof(reply);++i) { random=random*1664525U+1013904223U; reply[i]=(uint8_t)(random>>24); }
        reply[0]=0; reply[1]=0; reply[2]=(uint8_t)(iteration%18); reply[3]=0;
        enum itl_iwx_flush_result result=itl_iwx_flush_decode(reply,iteration%134,0,0xffff,&out);
        if (result==ITL_IWX_FLUSH_OK) {
            assert(out.count<=16);
            for (unsigned i=0;i<out.count;++i) {
                assert(out.entries[i].tid<=15 && out.entries[i].queue<512);
                for (unsigned j=0;j<i;++j) assert(out.entries[i].queue!=out.entries[j].queue);
            }
        }
    }
}
int main(void)
{
    test_management_tid_does_not_index_data_array(); test_literal_reply_and_unaligned_input();
    test_lengths_station_counts_and_empty_evidence(); test_full_count_and_invalid_tail_are_atomic();
    test_mutated_replies(); puts("IWX flush-v1 reply tests passed");
}
