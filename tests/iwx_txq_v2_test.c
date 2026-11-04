/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itlwm/hal_iwx/iwx_txq_v2.h"
static void test_flagged_response_rejected(void)
{
    uint8_t reply[8]={7,0,1,0,0,0,0,0};
    struct itl_iwx_txq_v2_response out,zero; memset(&zero,0,sizeof(zero));
    assert(itl_iwx_txq_v2_decode(reply,8,&out)==ITL_IWX_TXQ_V2_REJECTED);
    assert(memcmp(&out,&zero,sizeof(out))==0);
    reply[2]=0; reply[3]=1;
    assert(itl_iwx_txq_v2_decode(reply,8,&out)==ITL_IWX_TXQ_V2_REJECTED);
    assert(memcmp(&out,&zero,sizeof(out))==0);
}
static void test_response_bytes_lengths_and_alignment(void)
{
    const uint8_t reply[9]={0xa5,0x34,0x12,0,0,0xcd,0xab,0x99,0x88};
    struct itl_iwx_txq_v2_response out,zero; memset(&zero,0,sizeof(zero));
    assert(itl_iwx_txq_v2_decode(reply+1,8,&out)==ITL_IWX_TXQ_V2_OK);
    assert(out.queue_number==0x1234 && out.write_pointer==0xabcd);
    for (size_t length=0;length<8;++length) {
        memset(&out,0xa5,sizeof(out));
        assert(itl_iwx_txq_v2_decode(reply,length,&out)==ITL_IWX_TXQ_V2_INVALID);
        assert(memcmp(&out,&zero,sizeof(out))==0);
    }
    assert(itl_iwx_txq_v2_decode(reply,9,&out)==ITL_IWX_TXQ_V2_INVALID);
    assert(itl_iwx_txq_v2_decode(NULL,8,&out)==ITL_IWX_TXQ_V2_INVALID);
    assert(itl_iwx_txq_v2_decode(reply,8,NULL)==ITL_IWX_TXQ_V2_INVALID);
}
static void test_command_golden_bytes_and_guards(void)
{
    const uint8_t expected[24]={3,15,1,0,5,0,0,0,
        0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
    uint8_t out[26],before[26]; memset(before,0xa5,sizeof(before));
    for (size_t capacity=0;capacity<24;++capacity) {
        memcpy(out,before,sizeof(out));
        assert(itl_iwx_txq_v2_encode(out+1,capacity,3,15,256,0x1122334455667788ULL,0x8877665544332200ULL)==ITL_IWX_TXQ_V2_INVALID);
        assert(memcmp(out,before,sizeof(out))==0);
    }
    memcpy(out,before,sizeof(out));
    assert(itl_iwx_txq_v2_encode(out+1,24,3,15,256,0x1122334455667788ULL,0x8877665544332200ULL)==ITL_IWX_TXQ_V2_OK);
    assert(memcmp(out+1,expected,24)==0 && out[0]==0xa5 && out[25]==0xa5);
    // Current base Wi-Fi fields stay station0, managementTID15, enable-only.
    const uint8_t base[24]={0,15,1,0,7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    assert(itl_iwx_txq_v2_encode(out,24,0,15,1024,0,0)==ITL_IWX_TXQ_V2_OK);
    assert(memcmp(out,base,24)==0);
}
static void test_ring_encoding_boundaries_and_invalid_arguments(void)
{
    const struct { uint32_t slots; uint8_t exponent; } cases[]={
        {8,0},{16,1},{32,2},{64,3},{128,4},{256,5},{512,6},
        {1024,7},{2048,8},{4096,9},{8192,10},{16384,11},{32768,12},{65536,13}
    };
    uint8_t out[24], before[24]; memset(before,0xa5,sizeof(before));
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        assert(itl_iwx_txq_v2_encode(out,24,255,15,cases[i].slots,0,0)==ITL_IWX_TXQ_V2_OK);
        assert(out[0]==255 && out[4]==cases[i].exponent && out[5]==0 && out[6]==0 && out[7]==0);
    }
    const uint32_t invalid[]={0,1,4,7,9,255,65535,65537,UINT32_MAX};
    for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        memcpy(out,before,sizeof(out));
        assert(itl_iwx_txq_v2_encode(out,24,0,15,invalid[i],0,0)==ITL_IWX_TXQ_V2_INVALID);
        assert(memcmp(out,before,sizeof(out))==0);
    }
    assert(itl_iwx_txq_v2_encode(out,24,256,15,256,0,0)==ITL_IWX_TXQ_V2_INVALID);
    assert(itl_iwx_txq_v2_encode(out,24,0,16,256,0,0)==ITL_IWX_TXQ_V2_INVALID);
    assert(itl_iwx_txq_v2_encode(NULL,24,0,15,256,0,0)==ITL_IWX_TXQ_V2_INVALID);
}
int main(void)
{
    test_flagged_response_rejected(); test_response_bytes_lengths_and_alignment();
    test_command_golden_bytes_and_guards(); test_ring_encoding_boundaries_and_invalid_arguments();
    puts("IWX queue-v2 wire tests passed");
}
