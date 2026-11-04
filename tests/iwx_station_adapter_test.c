/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itlwm/hal_iwx/iwx_station_adapter.h"
static const struct itl_iwx_station_versions versions={12,2,2,0};
static const struct itl_iwx_station_context context={1,0x34,0};
static const uint8_t peer[6]={2,0x11,0x22,0x33,0x44,0x55};
static struct itl_iwx_awdl_resource_handle setup(struct itl_iwx_awdl_resources *p)
{
    struct itl_iwx_awdl_resource_config c={0};
    c.queue_lifetime=ITL_IWX_RESOURCE_STATION_BOUND;
    c.station_count=16; c.queue_count=512; c.station_mask=0xfff8; c.queue_mask[1]=4;
    assert(itl_iwx_awdl_resources_init(p,&c,1)==ITL_IWX_RESOURCE_OK);
    struct itl_iwx_awdl_resource_handle h;
    assert(itl_iwx_awdl_resources_reserve(p,peer,&h)==ITL_IWX_RESOURCE_OK);
    return h;
}
static struct itl_iwx_station_request prepare_add(struct itl_iwx_awdl_resources *p,
    const struct itl_iwx_awdl_resource_handle *h)
{
    struct itl_iwx_station_request r;
    assert(itl_iwx_station_prepare(p,h,&versions,&context,ITL_IWX_RESOURCE_ADD_STATION,&r)==ITL_IWX_RESOURCE_OK);
    return r;
}
static void transition(struct itl_iwx_awdl_resources *p,struct itl_iwx_awdl_resource_handle *h,
    enum itl_iwx_awdl_resource_action a,uint32_t queue)
{
    struct itl_iwx_awdl_resource_ticket t;
    assert(itl_iwx_awdl_resources_begin(p,h,a,&t)==ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_awdl_resources_complete(p,&t,ITL_IWX_RESOURCE_SUCCESS,queue)==ITL_IWX_RESOURCE_OK);
}
static void test_explicit_station_and_literal_bytes(void)
{
    struct itl_iwx_awdl_resources p; struct itl_iwx_awdl_resource_handle h=setup(&p);
    struct itl_iwx_station_request r=prepare_add(&p,&h);
    const uint8_t want[48]={
        0,0,0xff,0xff,1,0x34,0,0,2,0x11,0x22,0x33,0x44,0x55,0,0,
        3,0,0,0,0,0,0,0,0,0,0,0x3c,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    assert(r.group==1 && r.opcode==0x18 && r.version==12 && r.reply_version==2 && r.length==48);
    assert(r.ticket.handle.station==3 && r.ticket.action==ITL_IWX_RESOURCE_ADD_STATION);
    assert(memcmp(r.bytes,want,48)==0);
    const uint8_t success[5]={0xaa,1,0,0x80,0};
    assert(itl_iwx_station_finish_add(&p,&r,1,0,success+1,4)==ITL_IWX_RESOURCE_OK);
    struct itl_iwx_awdl_resources before=p;
    assert(itl_iwx_station_finish_add(&p,&r,1,0,success+1,4)==ITL_IWX_RESOURCE_STALE);
    assert(memcmp(&before,&p,sizeof(p))==0);
    struct itl_iwx_station_request rm;
    struct itl_iwx_station_versions invalid=versions; invalid.remove_reply=1;
    before=p;
    assert(itl_iwx_station_prepare(&p,&h,&invalid,NULL,ITL_IWX_RESOURCE_REMOVE_STATION,&rm)==ITL_IWX_RESOURCE_INVALID);
    invalid=versions; invalid.remove_command=99;
    assert(itl_iwx_station_prepare(&p,&h,&invalid,NULL,ITL_IWX_RESOURCE_REMOVE_STATION,&rm)==ITL_IWX_RESOURCE_INVALID);
    assert(memcmp(&before,&p,sizeof(p))==0);
    assert(itl_iwx_station_prepare(&p,&h,&versions,NULL,ITL_IWX_RESOURCE_REMOVE_STATION,&rm)==ITL_IWX_RESOURCE_OK);
    const uint8_t remove[4]={3,0,0,0};
    assert(rm.group==1 && rm.opcode==0x19 && rm.version==2 && rm.reply_version==0 && rm.length==4);
    assert(memcmp(rm.bytes,remove,4)==0);
    for (unsigned i=4;i<48;++i) assert(rm.bytes[i]==0);
    assert(itl_iwx_station_finish_remove(&p,&rm,1)==ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_awdl_resources_reserve(&p,peer,&h)==ITL_IWX_RESOURCE_OK && h.station==3);
}
static void test_bad_inputs_do_not_consume_a_ticket(void)
{
    struct itl_iwx_awdl_resources p; struct itl_iwx_awdl_resource_handle h=setup(&p);
    struct itl_iwx_awdl_resources before=p;
    struct itl_iwx_station_request r,zero; memset(&zero,0,sizeof(zero));
    const uint8_t bad[]={0,10,11,13,99,255};
    for (unsigned i=0;i<sizeof(bad);++i) {
        struct itl_iwx_station_versions v=versions; v.add_command=bad[i];
        memset(&r,0xa5,sizeof(r));
        assert(itl_iwx_station_prepare(&p,&h,&v,&context,ITL_IWX_RESOURCE_ADD_STATION,&r)==ITL_IWX_RESOURCE_INVALID);
        assert(memcmp(&r,&zero,sizeof(r))==0 && memcmp(&before,&p,sizeof(p))==0);
    }
    struct itl_iwx_station_versions v=versions; v.add_reply=1;
    assert(itl_iwx_station_prepare(&p,&h,&v,&context,ITL_IWX_RESOURCE_ADD_STATION,&r)==ITL_IWX_RESOURCE_INVALID);
    const struct itl_iwx_station_context invalid[]={{256,0,0},{0,256,0},{0,0,5},{UINT32_MAX,0,0}};
    for (unsigned i=0;i<4;++i)
        assert(itl_iwx_station_prepare(&p,&h,&versions,&invalid[i],ITL_IWX_RESOURCE_ADD_STATION,&r)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_station_prepare(&p,&h,&versions,NULL,ITL_IWX_RESOURCE_ADD_STATION,&r)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_station_prepare(&p,&h,NULL,&context,ITL_IWX_RESOURCE_ADD_STATION,&r)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_station_prepare(&p,&h,&versions,&context,ITL_IWX_RESOURCE_DRAIN_QUEUE,&r)==ITL_IWX_RESOURCE_STATE);
    assert(itl_iwx_station_prepare(&p,&h,&versions,&context,ITL_IWX_RESOURCE_ADD_STATION,NULL)==ITL_IWX_RESOURCE_INVALID);
    assert(memcmp(&before,&p,sizeof(p))==0);
    r=prepare_add(&p,&h); before=p;
    assert(itl_iwx_station_prepare(&p,&h,&versions,&context,ITL_IWX_RESOURCE_ADD_STATION,&r)==ITL_IWX_RESOURCE_STATE);
    assert(memcmp(&before,&p,sizeof(p))==0);
}
static void test_ambiguous_add_is_quarantined_not_released(void)
{
    const uint8_t status[5]={1,0,0,0,0};
    for (unsigned i=0;i<11;++i) {
        struct itl_iwx_awdl_resources p; struct itl_iwx_awdl_resource_handle h=setup(&p);
        struct itl_iwx_station_request r=prepare_add(&p,&h);
        uint8_t reply[5]; memcpy(reply,status,5);
        if (i==0) reply[0]=2;
        if (i==1) reply[0]=4;
        if (i==2) reply[0]=8;
        if (i==3) reply[0]=0;
        size_t length=i==10?5:(i>=6?(size_t)(i-6):4);
        assert(itl_iwx_station_finish_add(&p,&r,i!=4,i==5,reply,length)==ITL_IWX_RESOURCE_UNKNOWN);
        assert(p.entries[3].phase==ITL_IWX_RESOURCE_QUARANTINED);
        assert(itl_iwx_awdl_resources_reserve(&p,peer,&h)==ITL_IWX_RESOURCE_DUPLICATE);
    }
    struct itl_iwx_awdl_resources p; struct itl_iwx_awdl_resource_handle h=setup(&p);
    struct itl_iwx_station_request r=prepare_add(&p,&h);
    assert(itl_iwx_station_finish_add(&p,&r,1,0,NULL,4)==ITL_IWX_RESOURCE_UNKNOWN);
}
static void test_close_reset_and_legacy_removal_hold_queue(void)
{
    struct itl_iwx_awdl_resources p; struct itl_iwx_awdl_resource_handle h=setup(&p);
    struct itl_iwx_station_request add=prepare_add(&p,&h),rm;
    const uint8_t ok[4]={1,0,0,0};
    assert(itl_iwx_station_finish_add(&p,&add,1,0,ok,4)==ITL_IWX_RESOURCE_OK);
    transition(&p,&h,ITL_IWX_RESOURCE_ALLOC_QUEUE,10);
    struct itl_iwx_awdl_resources before=p;
    assert(itl_iwx_station_prepare(&p,&h,&versions,NULL,ITL_IWX_RESOURCE_REMOVE_STATION,&rm)==ITL_IWX_RESOURCE_STATE);
    assert(memcmp(&before,&p,sizeof(p))==0);
    transition(&p,&h,ITL_IWX_RESOURCE_DRAIN_QUEUE,0);
    assert(itl_iwx_station_prepare(&p,&h,&versions,NULL,ITL_IWX_RESOURCE_REMOVE_STATION,&rm)==ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_station_finish_remove(&p,&rm,1)==ITL_IWX_RESOURCE_OK);
    assert(p.entries[3].phase==ITL_IWX_RESOURCE_STATION_GONE && p.entries[3].has_queue);
    transition(&p,&h,ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE,0);
    assert(itl_iwx_awdl_resources_reserve(&p,peer,&h)==ITL_IWX_RESOURCE_OK);
    add=prepare_add(&p,&h);
    assert(itl_iwx_awdl_resources_close(&p,&h)==ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_station_finish_add(&p,&add,1,0,ok,4)==ITL_IWX_RESOURCE_OK);
    uint16_t q;
    assert(itl_iwx_awdl_resources_tx_queue(&p,&h,&q)!=ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_awdl_resources_reset(&p,2)==ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_awdl_resources_reserve(&p,peer,&h)==ITL_IWX_RESOURCE_OK);
    before=p;
    assert(itl_iwx_station_prepare(&p,&add.ticket.handle,&versions,&context,ITL_IWX_RESOURCE_ADD_STATION,&rm)==ITL_IWX_RESOURCE_STALE);
    assert(memcmp(&before,&p,sizeof(p))==0);
    assert(itl_iwx_station_finish_add(&p,&add,1,0,ok,4)==ITL_IWX_RESOURCE_STALE);
    assert(memcmp(&before,&p,sizeof(p))==0);
    add=prepare_add(&p,&h); before=p;
    assert(itl_iwx_station_finish_remove(&p,&add,1)==ITL_IWX_RESOURCE_INVALID);
    assert(memcmp(&before,&p,sizeof(p))==0);
    assert(itl_iwx_station_finish_add(&p,&add,1,0,ok,4)==ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_station_prepare(&p,&h,&versions,NULL,ITL_IWX_RESOURCE_REMOVE_STATION,&rm)==ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_station_finish_remove(&p,&rm,0)==ITL_IWX_RESOURCE_UNKNOWN);
    assert(p.entries[3].phase==ITL_IWX_RESOURCE_QUARANTINED);
}
int main(void)
{
    test_explicit_station_and_literal_bytes(); test_bad_inputs_do_not_consume_a_ticket();
    test_ambiguous_add_is_quarantined_not_released(); test_close_reset_and_legacy_removal_hold_queue();
    puts("IWX explicit station adapter tests passed");
}
