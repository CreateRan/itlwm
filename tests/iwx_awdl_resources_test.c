/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itlwm/hal_iwx/iwx_awdl_resources.h"
#include "itlwm/hal_iwx/iwx_txq_v2.h"

static const uint8_t alice[6] = {2,0,0,0,0,1};
static const uint8_t bob[6] = {2,0,0,0,0,2};
static struct itl_iwx_awdl_resource_config config(void)
{
    struct itl_iwx_awdl_resource_config c = {0};
    c.queue_lifetime=ITL_IWX_RESOURCE_SEPARATE_REMOVE;
    c.station_count = 16; c.queue_count = 512; c.station_mask = 0xfff8;
    c.queue_mask[1] = 0x0c; /* Exclusive grants 10, 11, 511. */
    c.queue_mask[63] = 0x80;
    return c;
}
static void init(struct itl_iwx_awdl_resources *p)
{
    struct itl_iwx_awdl_resource_config c = config();
    assert(itl_iwx_awdl_resources_init(p, &c, 100) == ITL_IWX_RESOURCE_OK);
}
static struct itl_iwx_awdl_resource_handle reserve(struct itl_iwx_awdl_resources *p, const uint8_t *peer)
{
    struct itl_iwx_awdl_resource_handle h;
    assert(itl_iwx_awdl_resources_reserve(p, peer, &h) == ITL_IWX_RESOURCE_OK);
    return h;
}
static struct itl_iwx_awdl_resource_ticket begin(struct itl_iwx_awdl_resources *p,
    struct itl_iwx_awdl_resource_handle h, enum itl_iwx_awdl_resource_action a)
{
    struct itl_iwx_awdl_resource_ticket t;
    assert(itl_iwx_awdl_resources_begin(p, &h, a, &t) == ITL_IWX_RESOURCE_OK);
    return t;
}
static void success(struct itl_iwx_awdl_resources *p, struct itl_iwx_awdl_resource_handle h,
    enum itl_iwx_awdl_resource_action a, uint32_t queue)
{
    struct itl_iwx_awdl_resource_ticket t = begin(p, h, a);
    assert(itl_iwx_awdl_resources_complete(p, &t, ITL_IWX_RESOURCE_SUCCESS, queue) == ITL_IWX_RESOURCE_OK);
}
static struct itl_iwx_awdl_resource_handle ready(struct itl_iwx_awdl_resources *p,
    const uint8_t *peer, uint32_t queue)
{
    struct itl_iwx_awdl_resource_handle h = reserve(p, peer);
    success(p, h, ITL_IWX_RESOURCE_ADD_STATION, 0);
    success(p, h, ITL_IWX_RESOURCE_ALLOC_QUEUE, queue);
    return h;
}
static void no_tx(struct itl_iwx_awdl_resources *p, struct itl_iwx_awdl_resource_handle h)
{
    uint16_t q = 999;
    assert(itl_iwx_awdl_resources_tx_queue(p, &h, &q) != ITL_IWX_RESOURCE_OK);
    assert(q == 0);
}
static void test_setup_order_and_teardown(void)
{
    struct itl_iwx_awdl_resources p; init(&p);
    struct itl_iwx_awdl_resource_handle h = reserve(&p, alice), next;
    struct itl_iwx_awdl_resource_ticket t;
    assert(h.station == 3 && h.epoch == 100 && h.lease != 0);
    no_tx(&p,h);
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_ALLOC_QUEUE,&t) == ITL_IWX_RESOURCE_STATE);
    success(&p,h,ITL_IWX_RESOURCE_ADD_STATION,0);
    t = begin(&p,h,ITL_IWX_RESOURCE_ALLOC_QUEUE);
    const uint8_t reply[8] = {10,0,0,0,0,0,0,0};
    struct itl_iwx_txq_v2_response wire;
    assert(itl_iwx_txq_v2_decode(reply,8,&wire) == ITL_IWX_TXQ_V2_OK);
    assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,wire.queue_number) == ITL_IWX_RESOURCE_OK);
    uint16_t q;
    assert(itl_iwx_awdl_resources_tx_queue(&p,&h,&q) == ITL_IWX_RESOURCE_OK && q == 10);
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_REMOVE_STATION,&t) == ITL_IWX_RESOURCE_STATE);
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_REMOVE_QUEUE,&t) == ITL_IWX_RESOURCE_STATE);
    t = begin(&p,h,ITL_IWX_RESOURCE_DRAIN_QUEUE); no_tx(&p,h);
    assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,0) == ITL_IWX_RESOURCE_OK);
    success(&p,h,ITL_IWX_RESOURCE_REMOVE_QUEUE,0); no_tx(&p,h);
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_ALLOC_QUEUE,&t) == ITL_IWX_RESOURCE_STATE);
    success(&p,h,ITL_IWX_RESOURCE_REMOVE_STATION,0);
    no_tx(&p,h); next = reserve(&p,bob);
    assert(next.station == 3 && next.lease != h.lease);
    assert(itl_iwx_awdl_resources_cancel(&p,&h) == ITL_IWX_RESOURCE_STALE);
    assert(itl_iwx_awdl_resources_cancel(&p,&next) == ITL_IWX_RESOURCE_OK);
}
static void test_duplicate_capacity_grants_and_cancel(void)
{
    struct itl_iwx_awdl_resources p; init(&p);
    struct itl_iwx_awdl_resource_handle h = reserve(&p,alice), out;
    struct itl_iwx_awdl_resources before = p;
    assert(itl_iwx_awdl_resources_reserve(&p,alice,&out) == ITL_IWX_RESOURCE_DUPLICATE);
    assert(!out.lease && memcmp(&before,&p,sizeof(p)) == 0);
    uint8_t peer[6] = {2,1,0,0,0,0};
    for (unsigned i=1; i<=12; ++i) { peer[5]=(uint8_t)i; out=reserve(&p,peer); assert(out.station == i+3); }
    peer[5]=13; before=p;
    assert(itl_iwx_awdl_resources_reserve(&p,peer,&out) == ITL_IWX_RESOURCE_FULL);
    assert(memcmp(&before,&p,sizeof(p)) == 0);
    assert(itl_iwx_awdl_resources_close(&p,&h) == ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_awdl_resources_cancel(&p,&h) == ITL_IWX_RESOURCE_OK);
    out=reserve(&p,bob); assert(out.station == 3);
    struct itl_iwx_awdl_resource_config c = config(); c.station_mask=0x8008;
    assert(itl_iwx_awdl_resources_init(&p,&c,200) == ITL_IWX_RESOURCE_OK); /* Fresh simulation only. */
    h=reserve(&p,alice); out=reserve(&p,bob); assert(h.station==3 && out.station==15);
}
static void test_late_callbacks_and_close_during_creation(void)
{
    for (unsigned close_stage=0; close_stage<3; ++close_stage) {
        struct itl_iwx_awdl_resources p; init(&p);
        struct itl_iwx_awdl_resource_handle h=reserve(&p,alice);
        struct itl_iwx_awdl_resource_ticket add=begin(&p,h,ITL_IWX_RESOURCE_ADD_STATION), q;
        if (close_stage==0) assert(itl_iwx_awdl_resources_close(&p,&h)==ITL_IWX_RESOURCE_OK);
        assert(itl_iwx_awdl_resources_complete(&p,&add,ITL_IWX_RESOURCE_SUCCESS,0)==ITL_IWX_RESOURCE_OK);
        if (close_stage==0) {
            assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_ALLOC_QUEUE,&q)==ITL_IWX_RESOURCE_STATE);
            success(&p,h,ITL_IWX_RESOURCE_REMOVE_STATION,0); continue;
        }
        q=begin(&p,h,ITL_IWX_RESOURCE_ALLOC_QUEUE);
        struct itl_iwx_awdl_resources before=p;
        assert(itl_iwx_awdl_resources_complete(&p,&add,ITL_IWX_RESOURCE_SUCCESS,0)==ITL_IWX_RESOURCE_STALE);
        assert(memcmp(&before,&p,sizeof(p))==0);
        if (close_stage==1) assert(itl_iwx_awdl_resources_close(&p,&h)==ITL_IWX_RESOURCE_OK);
        assert(itl_iwx_awdl_resources_complete(&p,&q,ITL_IWX_RESOURCE_SUCCESS,511)==ITL_IWX_RESOURCE_OK);
        if (close_stage==2) assert(itl_iwx_awdl_resources_close(&p,&h)==ITL_IWX_RESOURCE_OK);
        no_tx(&p,h);
        success(&p,h,ITL_IWX_RESOURCE_DRAIN_QUEUE,0);
        success(&p,h,ITL_IWX_RESOURCE_REMOVE_QUEUE,0);
        success(&p,h,ITL_IWX_RESOURCE_REMOVE_STATION,0);
        h=reserve(&p,alice); before=p;
        assert(itl_iwx_awdl_resources_complete(&p,&q,ITL_IWX_RESOURCE_SUCCESS,511)==ITL_IWX_RESOURCE_STALE);
        assert(memcmp(&before,&p,sizeof(p))==0);
    }
}
static void test_explicit_rejection_retry(void)
{
    struct itl_iwx_awdl_resources p; init(&p);
    struct itl_iwx_awdl_resource_handle h=reserve(&p,alice);
    struct itl_iwx_awdl_resource_ticket t=begin(&p,h,ITL_IWX_RESOURCE_ADD_STATION);
    assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_DENIED,0)==ITL_IWX_RESOURCE_REJECTED);
    h=reserve(&p,alice); success(&p,h,ITL_IWX_RESOURCE_ADD_STATION,0);
    t=begin(&p,h,ITL_IWX_RESOURCE_ALLOC_QUEUE);
    assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_DENIED,10)==ITL_IWX_RESOURCE_REJECTED);
    success(&p,h,ITL_IWX_RESOURCE_ALLOC_QUEUE,10);
    const enum itl_iwx_awdl_resource_action actions[]={ITL_IWX_RESOURCE_DRAIN_QUEUE,ITL_IWX_RESOURCE_REMOVE_QUEUE,ITL_IWX_RESOURCE_REMOVE_STATION};
    for (unsigned i=0;i<3;++i) {
        t=begin(&p,h,actions[i]);
        assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_DENIED,0)==ITL_IWX_RESOURCE_REJECTED);
        no_tx(&p,h);
        struct itl_iwx_awdl_resource_ticket retry=begin(&p,h,actions[i]);
        struct itl_iwx_awdl_resources before=p;
        assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,0)==ITL_IWX_RESOURCE_STALE);
        assert(memcmp(&before,&p,sizeof(p))==0);
        assert(itl_iwx_awdl_resources_complete(&p,&retry,ITL_IWX_RESOURCE_SUCCESS,0)==ITL_IWX_RESOURCE_OK);
    }
}
static void test_uncertain_outcomes_hold_ownership(void)
{
    for (unsigned action=1;action<=5;++action) {
        struct itl_iwx_awdl_resources p; init(&p);
        struct itl_iwx_awdl_resource_handle h=reserve(&p,alice), out;
        if (action>1) success(&p,h,ITL_IWX_RESOURCE_ADD_STATION,0);
        if (action>2) success(&p,h,ITL_IWX_RESOURCE_ALLOC_QUEUE,10);
        if (action>3) success(&p,h,ITL_IWX_RESOURCE_DRAIN_QUEUE,0);
        if (action>4) success(&p,h,ITL_IWX_RESOURCE_REMOVE_QUEUE,0);
        struct itl_iwx_awdl_resource_ticket t=begin(&p,h,(enum itl_iwx_awdl_resource_action)action);
        assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_UNCERTAIN,0)==ITL_IWX_RESOURCE_UNKNOWN);
        no_tx(&p,h);
        assert(p.entries[h.station].phase==ITL_IWX_RESOURCE_QUARANTINED);
        assert(itl_iwx_awdl_resources_cancel(&p,&h)==ITL_IWX_RESOURCE_STATE);
        if (action==2) {
            assert(p.fault==1);
            assert(itl_iwx_awdl_resources_reserve(&p,bob,&out)==ITL_IWX_RESOURCE_FAULT);
        } else {
            out=reserve(&p,bob); assert(out.station==4);
            assert(itl_iwx_awdl_resources_reserve(&p,alice,&out)==ITL_IWX_RESOURCE_DUPLICATE);
        }
        struct itl_iwx_awdl_resources before=p;
        assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,10)==ITL_IWX_RESOURCE_STALE);
        assert(memcmp(&before,&p,sizeof(p))==0);
        assert(itl_iwx_awdl_resources_reset(&p,101)==ITL_IWX_RESOURCE_OK);
        out=reserve(&p,alice); assert(out.station==3 && out.epoch==101); no_tx(&p,out);
        assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,10)==ITL_IWX_RESOURCE_STALE);
    }
}
static void test_queue_collision_and_invalid_ids_fault_globally(void)
{
    const uint32_t ids[]={10,9,12,512,UINT32_MAX};
    for (unsigned i=0;i<5;++i) {
        struct itl_iwx_awdl_resources p; init(&p);
        struct itl_iwx_awdl_resource_handle a=ready(&p,alice,10), b=reserve(&p,bob), out;
        success(&p,b,ITL_IWX_RESOURCE_ADD_STATION,0);
        struct itl_iwx_awdl_resource_ticket t=begin(&p,b,ITL_IWX_RESOURCE_ALLOC_QUEUE);
        assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,ids[i])==ITL_IWX_RESOURCE_CONFLICT);
        assert(p.fault && p.entries[3].has_queue && p.entries[3].queue==10);
        no_tx(&p,a); no_tx(&p,b);
        assert(itl_iwx_awdl_resources_reserve(&p,alice,&out)==ITL_IWX_RESOURCE_FAULT);
        success(&p,a,ITL_IWX_RESOURCE_DRAIN_QUEUE,0);
        success(&p,a,ITL_IWX_RESOURCE_REMOVE_QUEUE,0);
        success(&p,a,ITL_IWX_RESOURCE_REMOVE_STATION,0);
        assert(p.fault); /* Normal teardown cannot clear an ownership fault. */
    }
}
static void test_two_peers_and_reuse_only_after_confirmed_removal(void)
{
    struct itl_iwx_awdl_resources p; init(&p);
    struct itl_iwx_awdl_resource_handle a=ready(&p,alice,10), b=ready(&p,bob,11);
    uint16_t q=0;
    assert(itl_iwx_awdl_resources_tx_queue(&p,&a,&q)==ITL_IWX_RESOURCE_OK && q==10);
    assert(itl_iwx_awdl_resources_tx_queue(&p,&b,&q)==ITL_IWX_RESOURCE_OK && q==11);
    success(&p,a,ITL_IWX_RESOURCE_DRAIN_QUEUE,0);
    success(&p,a,ITL_IWX_RESOURCE_REMOVE_QUEUE,0);
    /* Station a is still allocated; only its confirmed-removed queue is free. */
    const uint8_t charlie[6]={2,0,0,0,0,3};
    struct itl_iwx_awdl_resource_handle c=ready(&p,charlie,10);
    assert(c.station==5);
    assert(itl_iwx_awdl_resources_tx_queue(&p,&c,&q)==ITL_IWX_RESOURCE_OK && q==10);
    assert(itl_iwx_awdl_resources_tx_queue(&p,&b,&q)==ITL_IWX_RESOURCE_OK && q==11);
    success(&p,a,ITL_IWX_RESOURCE_REMOVE_STATION,0);
    assert(itl_iwx_awdl_resources_tx_queue(&p,&c,&q)==ITL_IWX_RESOURCE_OK && q==10);
    for (unsigned action=3;action<=4;++action) {
        init(&p); a=ready(&p,alice,10);
        if (action==4) success(&p,a,ITL_IWX_RESOURCE_DRAIN_QUEUE,0);
        struct itl_iwx_awdl_resource_ticket t=begin(&p,a,(enum itl_iwx_awdl_resource_action)action);
        assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_UNCERTAIN,0)==ITL_IWX_RESOURCE_UNKNOWN);
        assert(p.entries[a.station].has_queue && p.entries[a.station].queue==10);
        b=reserve(&p,bob); success(&p,b,ITL_IWX_RESOURCE_ADD_STATION,0);
        t=begin(&p,b,ITL_IWX_RESOURCE_ALLOC_QUEUE);
        assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,10)==ITL_IWX_RESOURCE_CONFLICT);
        assert(p.entries[a.station].has_queue && p.entries[a.station].queue==10);
    }
}
static void test_empty_grants_and_last_serial_completion(void)
{
    struct itl_iwx_awdl_resources p;
    struct itl_iwx_awdl_resource_config c=config(); c.station_mask=0;
    assert(itl_iwx_awdl_resources_init(&p,&c,1)==ITL_IWX_RESOURCE_OK);
    struct itl_iwx_awdl_resource_handle h;
    assert(itl_iwx_awdl_resources_reserve(&p,alice,&h)==ITL_IWX_RESOURCE_FULL);
    c=config(); memset(c.queue_mask,0,sizeof(c.queue_mask));
    assert(itl_iwx_awdl_resources_init(&p,&c,2)==ITL_IWX_RESOURCE_OK);
    h=reserve(&p,alice); success(&p,h,ITL_IWX_RESOURCE_ADD_STATION,0);
    struct itl_iwx_awdl_resource_ticket t;
    struct itl_iwx_awdl_resources empty_before=p;
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_ALLOC_QUEUE,&t)==ITL_IWX_RESOURCE_FULL);
    assert(memcmp(&empty_before,&p,sizeof(p))==0);
    init(&p); h=reserve(&p,alice); p.serial=UINT64_MAX-1;
    t=begin(&p,h,ITL_IWX_RESOURCE_ADD_STATION); assert(t.serial==UINT64_MAX);
    assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,0)==ITL_IWX_RESOURCE_OK);
    struct itl_iwx_awdl_resources before=p;
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_ALLOC_QUEUE,&t)==ITL_IWX_RESOURCE_EXHAUSTED);
    assert(memcmp(&before,&p,sizeof(p))==0);
    assert(itl_iwx_awdl_resources_reset(&p,101)==ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_awdl_resources_close(&p,&h)==ITL_IWX_RESOURCE_STALE);
}
static void test_inflight_queue_capacity_is_reserved(void)
{
    struct itl_iwx_awdl_resources p;
    struct itl_iwx_awdl_resource_config c=config();
    memset(c.queue_mask,0,sizeof(c.queue_mask)); c.queue_mask[1]=0x04; /* Only 10. */
    assert(itl_iwx_awdl_resources_init(&p,&c,1)==ITL_IWX_RESOURCE_OK);
    struct itl_iwx_awdl_resource_handle a=reserve(&p,alice), b=reserve(&p,bob);
    success(&p,a,ITL_IWX_RESOURCE_ADD_STATION,0);
    success(&p,b,ITL_IWX_RESOURCE_ADD_STATION,0);
    struct itl_iwx_awdl_resource_ticket pending=begin(&p,a,ITL_IWX_RESOURCE_ALLOC_QUEUE), t;
    struct itl_iwx_awdl_resources before=p;
    assert(itl_iwx_awdl_resources_begin(&p,&b,ITL_IWX_RESOURCE_ALLOC_QUEUE,&t)==ITL_IWX_RESOURCE_FULL);
    assert(memcmp(&before,&p,sizeof(p))==0);
    assert(itl_iwx_awdl_resources_complete(&p,&pending,ITL_IWX_RESOURCE_DENIED,0)==ITL_IWX_RESOURCE_REJECTED);
    success(&p,b,ITL_IWX_RESOURCE_ALLOC_QUEUE,10);
    before=p;
    assert(itl_iwx_awdl_resources_begin(&p,&a,ITL_IWX_RESOURCE_ALLOC_QUEUE,&t)==ITL_IWX_RESOURCE_FULL);
    assert(memcmp(&before,&p,sizeof(p))==0);
    success(&p,b,ITL_IWX_RESOURCE_DRAIN_QUEUE,0);
    success(&p,b,ITL_IWX_RESOURCE_REMOVE_QUEUE,0);
    success(&p,a,ITL_IWX_RESOURCE_ALLOC_QUEUE,10);
}
static void test_legacy_station_then_host_queue_release(void)
{
    for (unsigned outcome=0;outcome<3;++outcome) {
        struct itl_iwx_awdl_resource_config c=config();
        c.queue_lifetime=ITL_IWX_RESOURCE_STATION_BOUND;
        struct itl_iwx_awdl_resources p;
        assert(itl_iwx_awdl_resources_init(&p,&c,1)==ITL_IWX_RESOURCE_OK);
        struct itl_iwx_awdl_resource_handle h=ready(&p,alice,10);
        struct itl_iwx_awdl_resource_ticket t;
        assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_REMOVE_STATION,&t)==ITL_IWX_RESOURCE_STATE);
        success(&p,h,ITL_IWX_RESOURCE_DRAIN_QUEUE,0);
        struct itl_iwx_awdl_resources before=p;
        assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_REMOVE_QUEUE,&t)==ITL_IWX_RESOURCE_STATE);
        assert(memcmp(&before,&p,sizeof(p))==0);
        t=begin(&p,h,ITL_IWX_RESOURCE_REMOVE_STATION);
        assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_DENIED,0)==ITL_IWX_RESOURCE_REJECTED);
        assert(p.entries[h.station].phase==ITL_IWX_RESOURCE_QUEUE_IDLE && p.entries[h.station].has_queue);
        success(&p,h,ITL_IWX_RESOURCE_REMOVE_STATION,0);
        assert(p.entries[h.station].phase==ITL_IWX_RESOURCE_STATION_GONE);
        assert(p.entries[h.station].has_queue && p.entries[h.station].queue==10);
        no_tx(&p,h);
        struct itl_iwx_awdl_resource_handle out;
        assert(itl_iwx_awdl_resources_reserve(&p,alice,&out)==ITL_IWX_RESOURCE_DUPLICATE);
        struct itl_iwx_awdl_resource_ticket release=begin(&p,h,ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE);
        assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,0)==ITL_IWX_RESOURCE_STALE);
        enum itl_iwx_awdl_resource_result expected=outcome==0?ITL_IWX_RESOURCE_OK:
            outcome==1?ITL_IWX_RESOURCE_REJECTED:ITL_IWX_RESOURCE_UNKNOWN;
        assert(itl_iwx_awdl_resources_complete(&p,&release,(enum itl_iwx_awdl_resource_outcome)outcome,0)==expected);
        if (outcome==1) {
            assert(p.entries[h.station].phase==ITL_IWX_RESOURCE_STATION_GONE && p.entries[h.station].has_queue);
            success(&p,h,ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE,0);
        }
        if (outcome==2) {
            assert(p.entries[h.station].phase==ITL_IWX_RESOURCE_QUARANTINED && p.entries[h.station].has_queue);
            assert(itl_iwx_awdl_resources_cancel(&p,&h)==ITL_IWX_RESOURCE_STATE);
            assert(itl_iwx_awdl_resources_reset(&p,2)==ITL_IWX_RESOURCE_OK);
        }
        out=reserve(&p,alice); assert(out.station==3); no_tx(&p,out);
        assert(itl_iwx_awdl_resources_complete(&p,&release,ITL_IWX_RESOURCE_SUCCESS,0)==ITL_IWX_RESOURCE_STALE);
    }
    struct itl_iwx_awdl_resources p; init(&p);
    struct itl_iwx_awdl_resource_handle h=ready(&p,alice,10);
    struct itl_iwx_awdl_resource_ticket t;
    success(&p,h,ITL_IWX_RESOURCE_DRAIN_QUEUE,0);
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE,&t)==ITL_IWX_RESOURCE_STATE);
    struct itl_iwx_awdl_resource_config c=config(); c.queue_lifetime=ITL_IWX_RESOURCE_STATION_BOUND;
    assert(itl_iwx_awdl_resources_init(&p,&c,2)==ITL_IWX_RESOURCE_OK);
    h=reserve(&p,alice);
    success(&p,h,ITL_IWX_RESOURCE_ADD_STATION,0);
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE,&t)==ITL_IWX_RESOURCE_STATE);
    success(&p,h,ITL_IWX_RESOURCE_REMOVE_STATION,0);
    struct itl_iwx_awdl_resource_handle out=reserve(&p,alice);
    assert(out.station==3 && out.lease!=h.lease);
}
static void test_legacy_unknown_station_removal_keeps_queue(void)
{
    struct itl_iwx_awdl_resource_config c=config();
    c.queue_lifetime=ITL_IWX_RESOURCE_STATION_BOUND;
    struct itl_iwx_awdl_resources p;
    assert(itl_iwx_awdl_resources_init(&p,&c,1)==ITL_IWX_RESOURCE_OK);
    struct itl_iwx_awdl_resource_handle h=ready(&p,alice,10);
    success(&p,h,ITL_IWX_RESOURCE_DRAIN_QUEUE,0);
    struct itl_iwx_awdl_resource_ticket t=begin(&p,h,ITL_IWX_RESOURCE_REMOVE_STATION);
    assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_UNCERTAIN,0)==ITL_IWX_RESOURCE_UNKNOWN);
    assert(p.entries[h.station].phase==ITL_IWX_RESOURCE_QUARANTINED && p.entries[h.station].has_queue);
    struct itl_iwx_awdl_resource_ticket release;
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE,&release)==ITL_IWX_RESOURCE_STATE);
    struct itl_iwx_awdl_resource_handle other=reserve(&p,bob);
    success(&p,other,ITL_IWX_RESOURCE_ADD_STATION,0);
    t=begin(&p,other,ITL_IWX_RESOURCE_ALLOC_QUEUE);
    assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,10)==ITL_IWX_RESOURCE_CONFLICT);
    assert(p.entries[h.station].has_queue && p.entries[h.station].queue==10);
}
static void test_validation_reset_and_counter_limits(void)
{
    struct itl_iwx_awdl_resources p; init(&p);
    struct itl_iwx_awdl_resources before=p;
    struct itl_iwx_awdl_resource_config c=config();
    assert(itl_iwx_awdl_resources_init(NULL,&c,100)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_awdl_resources_init(&p,NULL,100)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_awdl_resources_init(&p,&c,0)==ITL_IWX_RESOURCE_INVALID);
    c.station_mask|=1; assert(itl_iwx_awdl_resources_init(&p,&c,100)==ITL_IWX_RESOURCE_INVALID);
    c=config(); c.station_count=15; assert(itl_iwx_awdl_resources_init(&p,&c,100)==ITL_IWX_RESOURCE_INVALID);
    c=config(); c.queue_count=511; assert(itl_iwx_awdl_resources_init(&p,&c,100)==ITL_IWX_RESOURCE_INVALID);
    c=config(); c.queue_mask[0]=1; assert(itl_iwx_awdl_resources_init(&p,&c,100)==ITL_IWX_RESOURCE_INVALID);
    c=config(); c.queue_lifetime=ITL_IWX_RESOURCE_LIFETIME_UNKNOWN;
    assert(itl_iwx_awdl_resources_init(&p,&c,100)==ITL_IWX_RESOURCE_INVALID);
    c=config(); c.queue_lifetime=255;
    assert(itl_iwx_awdl_resources_init(&p,&c,100)==ITL_IWX_RESOURCE_INVALID);
    assert(memcmp(&before,&p,sizeof(p))==0);
    struct itl_iwx_awdl_resource_handle h,out;
    uint8_t invalid[6]={0};
    assert(itl_iwx_awdl_resources_reserve(&p,invalid,&out)==ITL_IWX_RESOURCE_INVALID);
    invalid[0]=1; assert(itl_iwx_awdl_resources_reserve(&p,invalid,&out)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_awdl_resources_reserve(&p,NULL,&out)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_awdl_resources_reserve(&p,alice,NULL)==ITL_IWX_RESOURCE_INVALID);
    assert(memcmp(&before,&p,sizeof(p))==0);
    h=reserve(&p,alice); struct itl_iwx_awdl_resource_ticket t;
    before=p;
    assert(itl_iwx_awdl_resources_begin(&p,&h,(enum itl_iwx_awdl_resource_action)0,&t)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_ADD_STATION,NULL)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_awdl_resources_reset(&p,100)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_awdl_resources_reset(&p,99)==ITL_IWX_RESOURCE_INVALID);
    assert(memcmp(&before,&p,sizeof(p))==0);
    t=begin(&p,h,ITL_IWX_RESOURCE_ADD_STATION); before=p;
    assert(itl_iwx_awdl_resources_complete(&p,&t,(enum itl_iwx_awdl_resource_outcome)3,0)==ITL_IWX_RESOURCE_INVALID);
    assert(memcmp(&before,&p,sizeof(p))==0);
    assert(itl_iwx_awdl_resources_reset(&p,UINT64_MAX)==ITL_IWX_RESOURCE_OK);
    h=reserve(&p,alice);
    assert(itl_iwx_awdl_resources_complete(&p,&t,ITL_IWX_RESOURCE_SUCCESS,0)==ITL_IWX_RESOURCE_STALE);
    p.serial=UINT64_MAX; before=p;
    assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_ADD_STATION,&t)==ITL_IWX_RESOURCE_EXHAUSTED);
    assert(itl_iwx_awdl_resources_reserve(&p,bob,&out)==ITL_IWX_RESOURCE_EXHAUSTED);
    assert(itl_iwx_awdl_resources_reset(&p,0)==ITL_IWX_RESOURCE_INVALID);
    assert(memcmp(&before,&p,sizeof(p))==0);
}
int main(void)
{
    test_setup_order_and_teardown(); test_duplicate_capacity_grants_and_cancel();
    test_late_callbacks_and_close_during_creation(); test_explicit_rejection_retry();
    test_uncertain_outcomes_hold_ownership(); test_queue_collision_and_invalid_ids_fault_globally();
    test_two_peers_and_reuse_only_after_confirmed_removal();
    test_empty_grants_and_last_serial_completion();
    test_inflight_queue_capacity_is_reserved();
    test_legacy_station_then_host_queue_release();
    test_legacy_unknown_station_removal_keeps_queue();
    test_validation_reset_and_counter_limits();
    puts("IWX AWDL resource ownership tests passed");
}
