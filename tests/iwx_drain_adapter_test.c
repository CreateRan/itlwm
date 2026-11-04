/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itlwm/hal_iwx/iwx_drain_adapter.h"
static const struct itl_iwx_drain_versions versions={12,2,2,1};
static const struct itl_iwx_station_context context={1,0x34,0};
static const uint8_t ok[4]={1,0,0,0};
static void transition(struct itl_iwx_awdl_resources *p,struct itl_iwx_awdl_resource_handle *h,
    enum itl_iwx_awdl_resource_action a,uint32_t queue)
{
    struct itl_iwx_awdl_resource_ticket t;
    assert(itl_iwx_awdl_resources_begin(p,h,a,&t)==ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_awdl_resources_complete(p,&t,ITL_IWX_RESOURCE_SUCCESS,queue)==ITL_IWX_RESOURCE_OK);
}
static struct itl_iwx_awdl_resource_handle ready(struct itl_iwx_awdl_resources *p)
{
    struct itl_iwx_awdl_resource_config c={0}; c.queue_lifetime=ITL_IWX_RESOURCE_STATION_BOUND;
    c.station_count=16; c.queue_count=512; c.station_mask=0xfff8; c.queue_mask[1]=0x0c;
    const uint8_t peer[6]={2,0,0,0,0,1}; struct itl_iwx_awdl_resource_handle h;
    assert(itl_iwx_awdl_resources_init(p,&c,1)==ITL_IWX_RESOURCE_OK);
    assert(itl_iwx_awdl_resources_reserve(p,peer,&h)==ITL_IWX_RESOURCE_OK);
    transition(p,&h,ITL_IWX_RESOURCE_ADD_STATION,0); transition(p,&h,ITL_IWX_RESOURCE_ALLOC_QUEUE,10);
    return h;
}
static struct itl_iwx_drain_session prepare(struct itl_iwx_awdl_resources *p,
    struct itl_iwx_awdl_resource_handle *h,struct itl_iwx_station_request *cmd)
{
    struct itl_iwx_drain_session s;
    assert(itl_iwx_drain_prepare(p,h,&versions,&context,15,&s,cmd)==ITL_IWX_RESOURCE_OK);
    return s;
}
static void reply(uint8_t *out,uint16_t before,uint16_t after)
{
    memset(out,0,132); out[0]=3;out[2]=1;out[4]=15;out[6]=10;
    out[8]=(uint8_t)before;out[9]=(uint8_t)(before>>8);out[10]=(uint8_t)after;out[11]=(uint8_t)(after>>8);
}
static struct itl_iwx_drain_host host(void)
{
    struct itl_iwx_drain_host h={0}; h.epoch=1;h.station=3;h.queue=10;h.tid=15;
    h.ring_count=256;h.sequence_modulus=65536;h.producer=1025;h.consumer=1025;
    h.tx_blocked=1;h.dma_quiescent=1;return h;
}
static void test_three_stage_success_and_literal_commands(void)
{
    struct itl_iwx_awdl_resources p;struct itl_iwx_awdl_resource_handle h=ready(&p);
    struct itl_iwx_station_request flow,flush;struct itl_iwx_drain_session s=prepare(&p,&h,&flow);
    const uint8_t want[48]={1,0,0,0,1,0x34,0,0,0,0,0,0,0,0,0,0,
        3,0,0,0,0,0x10,0,0,0,0x10,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    assert(flow.group==1&&flow.opcode==0x18&&flow.version==12&&flow.reply_version==2&&flow.length==48);
    assert(memcmp(flow.bytes,want,48)==0 && s.queue==10 && s.tid==15);
    uint16_t q;assert(itl_iwx_awdl_resources_tx_queue(&p,&h,&q)==ITL_IWX_RESOURCE_STATE);
    assert(itl_iwx_drain_flow_reply(&p,&s,&flow,1,0,ok,4,&flush)==ITL_IWX_RESOURCE_OK);
    const uint8_t fwant[8]={3,0,0,0,0,0x80,0,0};
    assert(flush.group==1&&flush.opcode==0x1e&&flush.version==2&&flush.reply_version==1&&flush.length==8);
    assert(memcmp(flush.bytes,fwant,8)==0 && s.phase==ITL_IWX_DRAIN_FLUSH);
    assert(p.entries[3].phase==ITL_IWX_RESOURCE_PENDING);
    uint8_t wire[132];reply(wire,1023,1025);
    assert(itl_iwx_drain_flush_reply(&p,&s,&flush,1,0,wire,132)==ITL_IWX_RESOURCE_OK);
    assert(s.phase==ITL_IWX_DRAIN_HOST&&s.read_after==1025);
    assert(p.entries[3].phase==ITL_IWX_RESOURCE_PENDING);
    struct itl_iwx_drain_host evidence=host();
    assert(itl_iwx_drain_confirm_host(&p,&s,&flow.ticket,&evidence)==ITL_IWX_RESOURCE_OK);
    assert(s.phase==ITL_IWX_DRAIN_DONE&&p.entries[3].phase==ITL_IWX_RESOURCE_QUEUE_IDLE&&p.entries[3].has_queue);
    assert(itl_iwx_drain_confirm_host(&p,&s,&flow.ticket,&evidence)==ITL_IWX_RESOURCE_STALE);
}
static void test_invalid_prepare_is_atomic(void)
{
    struct itl_iwx_awdl_resources p;struct itl_iwx_awdl_resource_handle h=ready(&p);
    struct itl_iwx_awdl_resources before=p;struct itl_iwx_drain_session s,z;struct itl_iwx_station_request cmd,cz;
    memset(&z,0,sizeof(z));memset(&cz,0,sizeof(cz));
    struct itl_iwx_drain_versions v=versions;
    for(unsigned i=0;i<4;++i){v=versions;((uint8_t *)&v)[i]=99;
        assert(itl_iwx_drain_prepare(&p,&h,&v,&context,15,&s,&cmd)==ITL_IWX_RESOURCE_INVALID);
        assert(memcmp(&s,&z,sizeof(s))==0&&memcmp(&cmd,&cz,sizeof(cmd))==0);
        assert(memcmp(&before,&p,sizeof(p))==0);}
    assert(itl_iwx_drain_prepare(&p,&h,&versions,&context,16,&s,&cmd)==ITL_IWX_RESOURCE_INVALID);
    assert(itl_iwx_drain_prepare(&p,&h,&versions,NULL,15,&s,&cmd)==ITL_IWX_RESOURCE_INVALID);
    assert(memcmp(&before,&p,sizeof(p))==0);
}
static void test_late_flow_and_reset_cannot_complete_flush(void)
{
    struct itl_iwx_awdl_resources p;struct itl_iwx_awdl_resource_handle h=ready(&p);
    struct itl_iwx_station_request flow,flush,unused;struct itl_iwx_drain_session s=prepare(&p,&h,&flow);
    assert(itl_iwx_drain_flow_reply(&p,&s,&flow,1,0,ok,4,&flush)==ITL_IWX_RESOURCE_OK);
    struct itl_iwx_awdl_resources before=p;struct itl_iwx_drain_session saved=s;
    assert(itl_iwx_drain_flow_reply(&p,&s,&flow,1,0,ok,4,&unused)==ITL_IWX_RESOURCE_STALE);
    uint8_t wire[132];reply(wire,1023,1025);
    assert(itl_iwx_drain_flush_reply(&p,&s,&flow,1,0,wire,132)==ITL_IWX_RESOURCE_STALE);
    assert(memcmp(&before,&p,sizeof(p))==0&&memcmp(&saved,&s,sizeof(s))==0);
    assert(itl_iwx_awdl_resources_reset(&p,2)==ITL_IWX_RESOURCE_OK);before=p;
    assert(itl_iwx_drain_flush_reply(&p,&s,&flush,1,0,wire,132)==ITL_IWX_RESOURCE_STALE);
    assert(memcmp(&before,&p,sizeof(p))==0);
}
static void test_bad_replies_quarantine_without_releasing_queue(void)
{
    for(unsigned i=0;i<11;++i){
        struct itl_iwx_awdl_resources p;struct itl_iwx_awdl_resource_handle h=ready(&p);
        struct itl_iwx_station_request flow,flush;struct itl_iwx_drain_session s=prepare(&p,&h,&flow);
        if(i<3){uint8_t status[4]={2,0,0,0};
            assert(itl_iwx_drain_flow_reply(&p,&s,&flow,i!=0,i==1,i==2?status:ok,4,&flush)==ITL_IWX_RESOURCE_UNKNOWN);
        }else{
            assert(itl_iwx_drain_flow_reply(&p,&s,&flow,1,0,ok,4,&flush)==ITL_IWX_RESOURCE_OK);
            uint8_t wire[132];reply(wire,0,1025);size_t length=132;
            if(i==3)length=0;if(i==4)wire[2]=0;if(i==5)wire[0]=4;
            if(i==6)wire[6]=11;if(i==7)wire[4]=0;if(i==8)length=131;
            if(i==10){wire[2]=2;wire[12]=15;wire[14]=11;} /* Extra queue is not our single-queue proof. */
            assert(itl_iwx_drain_flush_reply(&p,&s,&flush,1,i==9,wire,length)==ITL_IWX_RESOURCE_UNKNOWN);
        }
        assert(p.entries[3].phase==ITL_IWX_RESOURCE_QUARANTINED&&p.entries[3].has_queue&&p.entries[3].queue==10);
        assert(s.phase==ITL_IWX_DRAIN_DONE);
    }
}
static void test_host_wait_and_sequence_domain(void)
{
    struct itl_iwx_awdl_resources p;struct itl_iwx_awdl_resource_handle h=ready(&p);
    struct itl_iwx_station_request flow,flush;struct itl_iwx_drain_session s=prepare(&p,&h,&flow);
    assert(itl_iwx_drain_flow_reply(&p,&s,&flow,1,0,ok,4,&flush)==ITL_IWX_RESOURCE_OK);
    uint8_t wire[132];reply(wire,65534,65535);
    assert(itl_iwx_drain_flush_reply(&p,&s,&flush,1,0,wire,132)==ITL_IWX_RESOURCE_OK);
    struct itl_iwx_drain_host e=host();e.sequence_modulus=256;e.producer=255;e.consumer=254;e.queued=1;e.dma_quiescent=0;
    assert(itl_iwx_drain_confirm_host(&p,&s,&flow.ticket,&e)==ITL_IWX_RESOURCE_STATE);
    assert(p.entries[3].phase==ITL_IWX_RESOURCE_PENDING&&s.phase==ITL_IWX_DRAIN_HOST);
    e.consumer=255;e.queued=0;e.dma_quiescent=1;
    assert(itl_iwx_drain_confirm_host(&p,&s,&flow.ticket,&e)==ITL_IWX_RESOURCE_OK);
    for(unsigned i=0;i<7;++i){h=ready(&p);s=prepare(&p,&h,&flow);
        assert(itl_iwx_drain_flow_reply(&p,&s,&flow,1,0,ok,4,&flush)==ITL_IWX_RESOURCE_OK);reply(wire,0,1025);
        assert(itl_iwx_drain_flush_reply(&p,&s,&flush,1,0,wire,132)==ITL_IWX_RESOURCE_OK);e=host();
        if(i==0)e.epoch=2;if(i==1)e.station=4;if(i==2)e.queue=11;if(i==3)e.tid=0;
        if(i==4)e.tx_blocked=0;if(i==5)e.ring_count=0;if(i==6)e.sequence_modulus=128;
        assert(itl_iwx_drain_confirm_host(&p,&s,&flow.ticket,&e)==ITL_IWX_RESOURCE_UNKNOWN);
        assert(p.entries[3].phase==ITL_IWX_RESOURCE_QUARANTINED&&p.entries[3].has_queue);
    }
}
static void test_host_wait_guards_individually_and_geometry_edges(void)
{
    struct itl_iwx_awdl_resources p;struct itl_iwx_awdl_resource_handle h=ready(&p);
    struct itl_iwx_station_request flow,flush;struct itl_iwx_drain_session s=prepare(&p,&h,&flow);
    assert(itl_iwx_drain_flow_reply(&p,&s,&flow,1,0,ok,4,&flush)==ITL_IWX_RESOURCE_OK);
    uint8_t wire[132];reply(wire,0,1025);
    assert(itl_iwx_drain_flush_reply(&p,&s,&flush,1,0,wire,132)==ITL_IWX_RESOURCE_OK);
    struct itl_iwx_awdl_resources before=p;struct itl_iwx_drain_session saved=s;
    for(unsigned i=0;i<4;++i){struct itl_iwx_drain_host e=host();
        if(i==0)e.dma_quiescent=0;if(i==1)e.queued=1;if(i==2)e.producer=1024;
        if(i==3){e.producer=1024;e.consumer=1024;}
        assert(itl_iwx_drain_confirm_host(&p,&s,&flow.ticket,&e)==ITL_IWX_RESOURCE_STATE);
        assert(memcmp(&before,&p,sizeof(p))==0&&memcmp(&saved,&s,sizeof(s))==0);
    }
    const uint32_t sizes[]={8,65536};
    for(unsigned i=0;i<2;++i){h=ready(&p);s=prepare(&p,&h,&flow);
        assert(itl_iwx_drain_flow_reply(&p,&s,&flow,1,0,ok,4,&flush)==ITL_IWX_RESOURCE_OK);
        reply(wire,0,65535);
        assert(itl_iwx_drain_flush_reply(&p,&s,&flush,1,0,wire,132)==ITL_IWX_RESOURCE_OK);
        struct itl_iwx_drain_host e=host();e.ring_count=sizes[i];e.sequence_modulus=sizes[i];
        e.producer=sizes[i]-1;e.consumer=sizes[i]-1;
        assert(itl_iwx_drain_confirm_host(&p,&s,&flow.ticket,&e)==ITL_IWX_RESOURCE_OK);
    }
}
static void test_invalid_session_tid_does_not_shift_or_change_ownership(void)
{
    struct itl_iwx_awdl_resources p;struct itl_iwx_awdl_resource_handle h=ready(&p);
    struct itl_iwx_station_request flow,flush;struct itl_iwx_drain_session s=prepare(&p,&h,&flow);
    struct itl_iwx_awdl_resources before=p;
    const uint8_t invalid[]={16,31,255};
    for(unsigned i=0;i<3;++i){struct itl_iwx_drain_session bad=s;bad.tid=invalid[i];
        assert(itl_iwx_drain_flow_reply(&p,&bad,&flow,1,0,ok,4,&flush)==ITL_IWX_RESOURCE_STALE);
        assert(memcmp(&before,&p,sizeof(p))==0);}
}
static void test_abort_is_uncertain_and_retains_ownership(void)
{
    for(unsigned stage=0;stage<3;++stage){
        struct itl_iwx_awdl_resources p;struct itl_iwx_awdl_resource_handle h=ready(&p);
        struct itl_iwx_station_request flow,flush;struct itl_iwx_drain_session s=prepare(&p,&h,&flow);
        if(stage>0)assert(itl_iwx_drain_flow_reply(&p,&s,&flow,1,0,ok,4,&flush)==ITL_IWX_RESOURCE_OK);
        if(stage>1){uint8_t wire[132];reply(wire,0,1025);assert(itl_iwx_drain_flush_reply(&p,&s,&flush,1,0,wire,132)==ITL_IWX_RESOURCE_OK);}
        assert(itl_iwx_drain_abort(&p,&s)==ITL_IWX_RESOURCE_UNKNOWN);
        assert(p.entries[3].phase==ITL_IWX_RESOURCE_QUARANTINED&&p.entries[3].has_queue);
        assert(itl_iwx_drain_abort(&p,&s)==ITL_IWX_RESOURCE_STALE);
    }
}
int main(void){test_three_stage_success_and_literal_commands();test_invalid_prepare_is_atomic();
 test_late_flow_and_reset_cannot_complete_flush();test_bad_replies_quarantine_without_releasing_queue();
 test_host_wait_and_sequence_domain();test_host_wait_guards_individually_and_geometry_edges();
 test_invalid_session_tid_does_not_shift_or_change_ownership();
 test_abort_is_uncertain_and_retains_ownership();
 puts("IWX staged drain adapter tests passed");}
