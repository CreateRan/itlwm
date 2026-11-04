/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itlwm/hal_iwx/iwx_command_exchange.h"
static const struct itl_iwx_station_versions versions={12,2,2,0};
static const struct itl_iwx_station_context context={1,2,0};
static const struct itl_iwx_exchange_epoch epoch={1,7,9,11};
static struct itl_iwx_station_request pending(struct itl_iwx_awdl_resources *p)
{
 struct itl_iwx_awdl_resource_config c={0};c.queue_lifetime=ITL_IWX_RESOURCE_STATION_BOUND;
 c.station_count=16;c.queue_count=512;c.station_mask=0xfff8;c.queue_mask[1]=4;
 const uint8_t peer[6]={2,0,0,0,0,1};struct itl_iwx_awdl_resource_handle h;struct itl_iwx_station_request r;
 assert(itl_iwx_awdl_resources_init(p,&c,1)==ITL_IWX_RESOURCE_OK);
 assert(itl_iwx_awdl_resources_reserve(p,peer,&h)==ITL_IWX_RESOURCE_OK);
 assert(itl_iwx_station_prepare(p,&h,&versions,&context,ITL_IWX_RESOURCE_ADD_STATION,&r)==ITL_IWX_RESOURCE_OK);return r;
}
static struct itl_iwx_exchange_plan plan(struct itl_iwx_awdl_resources *p,const struct itl_iwx_station_request *r)
{
 struct itl_iwx_exchange_plan out;
 assert(itl_iwx_exchange_prepare(p,r,&epoch,&epoch,31,r->version,r->reply_version,&out)==ITL_IWX_RESOURCE_OK);return out;
}
static void test_layout_version_is_not_wire_header_version(void)
{
 struct itl_iwx_awdl_resources p;struct itl_iwx_station_request r=pending(&p);
 struct itl_iwx_awdl_resources before=p;struct itl_iwx_exchange_plan tx=plan(&p,&r);
 assert(tx.host_command_id==0x18 && tx.payload_length==4 && tx.slot==31 && tx.opcode==0x18);
 assert(tx.ticket.serial==r.ticket.serial&&memcmp(&before,&p,sizeof(p))==0);
 const uint8_t wire[12]={8,0,0,0,0x18,1,31,0,1,0,0,0};
 struct itl_iwx_exchange_reply reply;
 assert(itl_iwx_exchange_capture(&p,&tx,&epoch,0,wire,12,&reply)==ITL_IWX_RESOURCE_OK);
 assert(reply.length==4&&reply.bytes[0]==1&&p.entries[3].phase==ITL_IWX_RESOURCE_PENDING);
 assert(itl_iwx_station_finish_add(&p,&r,1,0,reply.bytes,reply.length)==ITL_IWX_RESOURCE_OK);
 assert(itl_iwx_exchange_capture(&p,&tx,&epoch,0,wire,12,&reply)==ITL_IWX_RESOURCE_STALE);
}
static void test_wrong_identity_or_frame_never_returns_payload(void)
{
 struct itl_iwx_awdl_resources p;struct itl_iwx_station_request r=pending(&p);
 struct itl_iwx_exchange_plan tx=plan(&p,&r);struct itl_iwx_exchange_reply reply,zero;
 memset(&zero,0,sizeof(zero));struct itl_iwx_awdl_resources before=p;
 for(unsigned i=0;i<10;++i){uint8_t wire[12]={8,0,0,0,0x18,1,31,0,1,0,0,0};size_t size=12;
  if(i==0)wire[4]=0x19;if(i==1)wire[5]=5;if(i==2)wire[5]=0x41;if(i==3)wire[6]=30;
  if(i==4)wire[7]=0x80;if(i==5)wire[0]=3;if(i==6)wire[0]=9;if(i==7)size=7;
  if(i==8)wire[0]=4;
  memset(&reply,0xa5,sizeof(reply));
  assert(itl_iwx_exchange_capture(&p,&tx,&epoch,i==9,wire,size,&reply)==ITL_IWX_RESOURCE_UNKNOWN);
  assert(memcmp(&reply,&zero,sizeof(reply))==0&&memcmp(&p,&before,sizeof(p))==0);
 }
 for(unsigned i=0;i<4;++i){struct itl_iwx_exchange_epoch changed=epoch;
  if(i==0)changed.ledger_epoch++;if(i==1)changed.driver_generation++;
  if(i==2)changed.transport_epoch++;if(i==3)changed.dma_epoch++;
  const uint8_t wire[12]={8,0,0,0,0x18,1,31,0,1,0,0,0};
  assert(itl_iwx_exchange_capture(&p,&tx,&changed,0,wire,12,&reply)==ITL_IWX_RESOURCE_STALE);
  assert(memcmp(&p,&before,sizeof(p))==0);
 }
}
static void test_invalid_prepare_is_atomic_and_failed_output_zero(void)
{
 struct itl_iwx_awdl_resources p;struct itl_iwx_station_request r=pending(&p);
 struct itl_iwx_awdl_resources before=p;struct itl_iwx_exchange_plan out,zero;memset(&zero,0,sizeof(zero));
 for(unsigned i=0;i<8;++i){struct itl_iwx_station_request bad=r;struct itl_iwx_exchange_epoch b=epoch;
  uint32_t slot=31,cv=12,rv=2;
  if(i==0)slot=256;if(i==1)cv=99;if(i==2)rv=99;if(i==3)bad.bytes[16]=0;
  if(i==4)bad.group=5;if(i==5)bad.length=49;if(i==6)b.transport_epoch=0;if(i==7)bad.bytes[8]=0xff;
  memset(&out,0xa5,sizeof(out));
  assert(itl_iwx_exchange_prepare(&p,&bad,&b,&epoch,slot,cv,rv,&out)!=ITL_IWX_RESOURCE_OK);
  assert(memcmp(&out,&zero,sizeof(out))==0&&memcmp(&before,&p,sizeof(p))==0);
 }
}
static void test_legacy_response_group_and_empty_remove_ack(void)
{
 struct itl_iwx_awdl_resources p;struct itl_iwx_station_request r=pending(&p);
 struct itl_iwx_exchange_plan tx=plan(&p,&r);struct itl_iwx_exchange_reply reply;
 uint8_t wire[13]={8,0,0,0,0x18,0,31,0,1,0,0,0,0xaa};
 assert(itl_iwx_exchange_capture(&p,&tx,&epoch,0,wire,13,&reply)==ITL_IWX_RESOURCE_OK);
 assert(itl_iwx_station_finish_add(&p,&r,1,0,reply.bytes,4)==ITL_IWX_RESOURCE_OK);
 struct itl_iwx_awdl_resource_handle h=r.ticket.handle;
 assert(itl_iwx_station_prepare(&p,&h,&versions,NULL,ITL_IWX_RESOURCE_REMOVE_STATION,&r)==ITL_IWX_RESOURCE_OK);
 tx=plan(&p,&r);assert(tx.host_command_id==0x19&&tx.payload_length==0);
 const uint8_t ack[8]={4,0,0,0,0x19,1,31,0};
 assert(itl_iwx_exchange_capture(&p,&tx,&epoch,0,ack,8,&reply)==ITL_IWX_RESOURCE_OK&&reply.length==0);
 assert(p.entries[3].phase==ITL_IWX_RESOURCE_PENDING); /* Transport ACK is not station absence. */
}
static void test_exchange_to_drain_chain_preserves_pending_ownership(void)
{
 struct itl_iwx_awdl_resources p;struct itl_iwx_station_request add=pending(&p),flow,flush;
 const uint8_t ok[4]={1,0,0,0};
 assert(itl_iwx_station_finish_add(&p,&add,1,0,ok,4)==ITL_IWX_RESOURCE_OK);
 struct itl_iwx_awdl_resource_handle h=add.ticket.handle;struct itl_iwx_awdl_resource_ticket allocation;
 assert(itl_iwx_awdl_resources_begin(&p,&h,ITL_IWX_RESOURCE_ALLOC_QUEUE,&allocation)==ITL_IWX_RESOURCE_OK);
 assert(itl_iwx_awdl_resources_complete(&p,&allocation,ITL_IWX_RESOURCE_SUCCESS,10)==ITL_IWX_RESOURCE_OK);
 const struct itl_iwx_drain_versions v={12,2,2,1};struct itl_iwx_drain_session drain;
 assert(itl_iwx_drain_prepare(&p,&h,&v,&context,15,&drain,&flow)==ITL_IWX_RESOURCE_OK);
 struct itl_iwx_exchange_plan tx=plan(&p,&flow);struct itl_iwx_exchange_reply reply;
 const uint8_t status[12]={8,0,0,0,0x18,1,31,0,1,0,0,0};
 assert(itl_iwx_exchange_capture(&p,&tx,&epoch,0,status,12,&reply)==ITL_IWX_RESOURCE_OK);
 assert(itl_iwx_drain_flow_reply(&p,&drain,&flow,1,0,reply.bytes,reply.length,&flush)==ITL_IWX_RESOURCE_OK);
 tx=plan(&p,&flush);assert(tx.host_command_id==0x1e&&tx.payload_length==132);
 struct itl_iwx_station_request bad=flush;bad.bytes[4]=3;
 struct itl_iwx_exchange_plan invalid;
 assert(itl_iwx_exchange_prepare(&p,&bad,&epoch,&epoch,31,2,1,&invalid)==ITL_IWX_RESOURCE_INVALID);
 uint8_t frame[140]={136,0,0,0,0x1e,1,31,0,3,0,1,0,15,0,10,0,0,0,3,0};
 assert(itl_iwx_exchange_capture(&p,&tx,&epoch,0,frame,140,&reply)==ITL_IWX_RESOURCE_OK);
 assert(itl_iwx_drain_flush_reply(&p,&drain,&flush,1,0,reply.bytes,reply.length)==ITL_IWX_RESOURCE_OK);
 assert(drain.phase==ITL_IWX_DRAIN_HOST&&p.entries[3].phase==ITL_IWX_RESOURCE_PENDING&&p.entries[3].has_queue);
 for(size_t length=0;length<8;++length)
  assert(itl_iwx_exchange_capture(&p,&tx,&epoch,0,frame,length,&reply)==ITL_IWX_RESOURCE_UNKNOWN);
}
static void test_global_fault_blocks_new_exchange_and_reply_exposure(void)
{
 struct itl_iwx_awdl_resources p;struct itl_iwx_station_request r=pending(&p);
 struct itl_iwx_exchange_plan tx=plan(&p,&r),out;struct itl_iwx_exchange_reply reply;
 p.fault=1;struct itl_iwx_awdl_resources before=p;
 assert(itl_iwx_exchange_prepare(&p,&r,&epoch,&epoch,31,12,2,&out)==ITL_IWX_RESOURCE_FAULT);
 const uint8_t wire[12]={8,0,0,0,0x18,1,31,0,1,0,0,0};
 assert(itl_iwx_exchange_capture(&p,&tx,&epoch,0,wire,12,&reply)==ITL_IWX_RESOURCE_FAULT);
 assert(memcmp(&before,&p,sizeof(p))==0);
}
static void test_stop_epoch_never_wraps_or_reactivates(void)
{
 assert(itl_iwx_exchange_next_epoch(1)==2);
 assert(itl_iwx_exchange_next_epoch(UINT64_MAX-1)==UINT64_MAX);
 assert(itl_iwx_exchange_next_epoch(UINT64_MAX)==0);
 assert(itl_iwx_exchange_next_epoch(0)==0);
}
int main(void){test_layout_version_is_not_wire_header_version();test_wrong_identity_or_frame_never_returns_payload();
 test_invalid_prepare_is_atomic_and_failed_output_zero();test_legacy_response_group_and_empty_remove_ack();
 test_exchange_to_drain_chain_preserves_pending_ownership();
 test_global_fault_blocks_new_exchange_and_reply_exposure();test_stop_epoch_never_wraps_or_reactivates();puts("IWX command exchange tests passed");}
