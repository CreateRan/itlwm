/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL_IWX_COMMAND_EXCHANGE_H
#define ITL_IWX_COMMAND_EXCHANGE_H
#include "iwx_drain_adapter.h"
struct itl_iwx_exchange_epoch {
    uint64_t ledger_epoch;
    int64_t driver_generation;
    uint64_t transport_epoch, dma_epoch;
};
struct itl_iwx_exchange_plan {
    struct itl_iwx_awdl_resource_ticket ticket;
    struct itl_iwx_exchange_epoch epoch;
    uint32_t host_command_id;
    uint16_t payload_length;
    uint8_t slot, opcode;
};
struct itl_iwx_exchange_reply { uint16_t length; uint8_t bytes[132]; };
/* Shared policy for a synchronous legacy-command exchange. All storage is
 * per-driver-instance and externally serialized; plans/requests are immutable.
 * Layout versions are not wire header versions: use the bare opcode so the
 * existing send path marks CMD_IS_NARROW and normalizes LONG_GROUP responses.
 */
static inline uint64_t itl_iwx_exchange_next_epoch(uint64_t epoch)
{ return epoch && epoch < UINT64_MAX ? epoch + 1 : 0; }
static inline int
itl_iwx_exchange_same_epoch(const struct itl_iwx_exchange_epoch *a,const struct itl_iwx_exchange_epoch *b)
{
 return a&&b&&a->ledger_epoch&&a->transport_epoch&&
  a->ledger_epoch==b->ledger_epoch&&a->driver_generation==b->driver_generation&&
  a->transport_epoch==b->transport_epoch&&a->dma_epoch==b->dma_epoch;
}
static inline int
itl_iwx_exchange_pending(const struct itl_iwx_awdl_resources *p,const struct itl_iwx_awdl_resource_ticket *t)
{
 if(!p||p->configured!=1||!t||!t->serial)return 0;
 int slot=itl_iwx_awdl_resources_find(p,&t->handle);
 if(slot<0)return 0;
 const struct itl_iwx_awdl_resource_entry *e=&p->entries[slot];
 return e->phase==ITL_IWX_RESOURCE_PENDING&&e->operation==t->serial&&e->action==t->action;
}
static inline enum itl_iwx_awdl_resource_result
itl_iwx_exchange_prepare(const struct itl_iwx_awdl_resources *p,
 const struct itl_iwx_station_request *request,const struct itl_iwx_exchange_epoch *bound,
 const struct itl_iwx_exchange_epoch *current,uint32_t slot,uint32_t command_version,
 uint32_t reply_version,struct itl_iwx_exchange_plan *out)
{
 if(!out)return ITL_IWX_RESOURCE_INVALID;
 memset(out,0,sizeof(*out));
 if(!p||p->configured!=1||!request||!bound||!current||slot>255)return ITL_IWX_RESOURCE_INVALID;
 if(p->fault)return ITL_IWX_RESOURCE_FAULT;
 if(!itl_iwx_exchange_same_epoch(bound,current)||current->ledger_epoch!=p->epoch||
  request->ticket.handle.epoch!=p->epoch||!itl_iwx_exchange_pending(p,&request->ticket))
  return ITL_IWX_RESOURCE_STALE;
 if(request->group!=1||command_version!=request->version||reply_version!=request->reply_version)
  return ITL_IWX_RESOURCE_INVALID;
 uint8_t expected[48]={0};uint16_t response=0;
 uint16_t station=request->ticket.handle.station;
 if(request->opcode==0x18&&request->version==12&&request->reply_version==2&&request->length==48){
  expected[4]=request->bytes[4];expected[5]=request->bytes[5];expected[16]=(uint8_t)station;
  if(request->ticket.action==ITL_IWX_RESOURCE_ADD_STATION){
   if(request->bytes[35]>4)return ITL_IWX_RESOURCE_INVALID;
   expected[2]=0xff;expected[3]=0xff;expected[27]=0x3c;expected[35]=request->bytes[35];
   memcpy(expected+8,p->entries[station].peer,6);
  }else if(request->ticket.action==ITL_IWX_RESOURCE_DRAIN_QUEUE){
   expected[0]=1;expected[21]=0x10;expected[25]=0x10;
  }else return ITL_IWX_RESOURCE_INVALID;
  response=4;
 }else if(request->opcode==0x19&&request->version==2&&request->reply_version==0&&request->length==4&&
  request->ticket.action==ITL_IWX_RESOURCE_REMOVE_STATION){expected[0]=(uint8_t)station;
 }else if(request->opcode==0x1e&&request->version==2&&request->reply_version==1&&request->length==8&&
  request->ticket.action==ITL_IWX_RESOURCE_DRAIN_QUEUE){
  uint16_t mask=(uint16_t)(request->bytes[4]|((uint16_t)request->bytes[5]<<8));
  if(!mask||(mask&(mask-1)))return ITL_IWX_RESOURCE_INVALID;
  expected[0]=(uint8_t)station;expected[4]=request->bytes[4];expected[5]=request->bytes[5];response=132;
 }else return ITL_IWX_RESOURCE_INVALID;
 if(memcmp(expected,request->bytes,sizeof(expected)))return ITL_IWX_RESOURCE_INVALID;
 out->ticket=request->ticket;out->epoch=*bound;out->host_command_id=request->opcode;
 out->payload_length=response;out->slot=(uint8_t)slot;out->opcode=request->opcode;
 return ITL_IWX_RESOURCE_OK;
}
/* Capture validates an owned response allocation, never frees or completes a
 * ledger operation. A valid empty REMOVE ack is transport evidence only.
 * available is the real allocated capacity, not a length read from the frame.
 */
static inline enum itl_iwx_awdl_resource_result
itl_iwx_exchange_capture(const struct itl_iwx_awdl_resources *p,
 const struct itl_iwx_exchange_plan *plan,const struct itl_iwx_exchange_epoch *current,
 int transport_error,const void *frame,size_t available,struct itl_iwx_exchange_reply *out)
{
 if(!out)return ITL_IWX_RESOURCE_INVALID;
 memset(out,0,sizeof(*out));
 if(!p||p->configured!=1||!plan||!current)return ITL_IWX_RESOURCE_INVALID;
 if(p->fault)return ITL_IWX_RESOURCE_FAULT;
 if(!itl_iwx_exchange_same_epoch(&plan->epoch,current)||current->ledger_epoch!=p->epoch||
  !itl_iwx_exchange_pending(p,&plan->ticket))return ITL_IWX_RESOURCE_STALE;
 if(transport_error||!frame||available<8||plan->payload_length>sizeof(out->bytes))return ITL_IWX_RESOURCE_UNKNOWN;
 const uint8_t *b=(const uint8_t *)frame;
 uint32_t flags=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
 size_t length=flags&0x3fff;
 if(length<4||length+4>available||length!=4U+plan->payload_length||b[4]!=plan->opcode||
  (b[5]!=0&&b[5]!=1)||b[6]!=plan->slot||b[7]!=0)return ITL_IWX_RESOURCE_UNKNOWN;
 out->length=plan->payload_length;
 memcpy(out->bytes,b+8,out->length);
 return ITL_IWX_RESOURCE_OK;
}
#endif
