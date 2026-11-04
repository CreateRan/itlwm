/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL_IWX_DRAIN_ADAPTER_H
#define ITL_IWX_DRAIN_ADAPTER_H
#include "iwx_station_adapter.h"
#include "iwx_flush_v1.h"
enum itl_iwx_drain_phase { ITL_IWX_DRAIN_FLOW=1, ITL_IWX_DRAIN_FLUSH, ITL_IWX_DRAIN_HOST, ITL_IWX_DRAIN_DONE };
struct itl_iwx_drain_versions { uint8_t station_command, station_reply, flush_command, flush_reply; };
struct itl_iwx_drain_session {
    struct itl_iwx_awdl_resource_ticket ticket;
    uint8_t phase, tid;
    uint16_t queue, read_before, read_after;
};
struct itl_iwx_drain_host {
    uint64_t epoch;
    uint16_t station, queue, tid;
    uint32_t ring_count, sequence_modulus, producer, consumer, queued;
    uint8_t tx_blocked, dma_quiescent;
};
/* Allocation-free offline orchestration. The caller serializes access, keeps
 * each issued command immutable, and correlates transport callbacks to that
 * original command and firmware incarnation. All inputs/outputs are separate
 * storage; prepare requires fresh output session storage. No dispatch or DMA
 * operation is performed. TID/context/binding capability comes from the caller.
 */
static inline int
itl_iwx_drain_same_ticket(const struct itl_iwx_awdl_resource_ticket *a,
    const struct itl_iwx_awdl_resource_ticket *b)
{
    return a && b && a->serial == b->serial && a->action == b->action &&
        a->handle.epoch == b->handle.epoch && a->handle.lease == b->handle.lease &&
        a->handle.station == b->handle.station;
}
static inline enum itl_iwx_awdl_resource_result
itl_iwx_drain_active(const struct itl_iwx_awdl_resources *p,const struct itl_iwx_drain_session *s)
{
    if (!p || p->configured != 1 || !s)
        return ITL_IWX_RESOURCE_INVALID;
    int slot = itl_iwx_awdl_resources_find(p,&s->ticket.handle);
    if (slot < 0 || s->tid > 15 || s->phase < ITL_IWX_DRAIN_FLOW || s->phase >= ITL_IWX_DRAIN_DONE ||
        s->ticket.action != ITL_IWX_RESOURCE_DRAIN_QUEUE)
        return ITL_IWX_RESOURCE_STALE;
    const struct itl_iwx_awdl_resource_entry *e = &p->entries[slot];
    if (e->phase != ITL_IWX_RESOURCE_PENDING || e->action != ITL_IWX_RESOURCE_DRAIN_QUEUE ||
        e->operation != s->ticket.serial || !e->has_queue || e->queue != s->queue || !e->closing)
        return ITL_IWX_RESOURCE_STALE;
    return ITL_IWX_RESOURCE_OK;
}
static inline enum itl_iwx_awdl_resource_result
itl_iwx_drain_abort(struct itl_iwx_awdl_resources *p,struct itl_iwx_drain_session *s)
{
    enum itl_iwx_awdl_resource_result result = itl_iwx_drain_active(p,s);
    if (result != ITL_IWX_RESOURCE_OK)
        return result;
    result = itl_iwx_awdl_resources_complete(p,&s->ticket,ITL_IWX_RESOURCE_UNCERTAIN,0);
    if (result == ITL_IWX_RESOURCE_UNKNOWN)
        s->phase = ITL_IWX_DRAIN_DONE;
    return result;
}
static inline enum itl_iwx_awdl_resource_result
itl_iwx_drain_prepare(struct itl_iwx_awdl_resources *p,
    const struct itl_iwx_awdl_resource_handle *h, const struct itl_iwx_drain_versions *v,
    const struct itl_iwx_station_context *c, uint32_t tid,
    struct itl_iwx_drain_session *out, struct itl_iwx_station_request *command)
{
    struct itl_iwx_drain_session draft;
    struct itl_iwx_station_request cmd;
    if (out) memset(out,0,sizeof(*out));
    if (command) memset(command,0,sizeof(*command));
    if (!out || !command || !p || p->configured != 1 || !h || !v || !c || tid > 15 ||
        c->id > 255 || c->color > 255 || v->station_command != 12 || v->station_reply != 2 ||
        v->flush_command != 2 || v->flush_reply != 1)
        return ITL_IWX_RESOURCE_INVALID;
    int slot = itl_iwx_awdl_resources_find(p,h);
    if (slot < 0) return ITL_IWX_RESOURCE_STALE;
    if (p->entries[slot].phase != ITL_IWX_RESOURCE_READY || !p->entries[slot].has_queue)
        return ITL_IWX_RESOURCE_STATE;
    memset(&draft,0,sizeof(draft)); memset(&cmd,0,sizeof(cmd));
    draft.queue = p->entries[slot].queue;
    draft.tid = (uint8_t)tid; draft.phase = ITL_IWX_DRAIN_FLOW;
    cmd.group = 1; cmd.opcode = 0x18; cmd.version = 12; cmd.reply_version = 2; cmd.length = 48;
    cmd.bytes[0] = 1; /* STA_MODE_MODIFY. */
    cmd.bytes[4] = (uint8_t)c->id; cmd.bytes[5] = (uint8_t)c->color;
    cmd.bytes[16] = (uint8_t)h->station;
    cmd.bytes[21] = 0x10; cmd.bytes[25] = 0x10; /* DRAIN_FLOW and its mask. */
    enum itl_iwx_awdl_resource_result result =
        itl_iwx_awdl_resources_begin(p,h,ITL_IWX_RESOURCE_DRAIN_QUEUE,&draft.ticket);
    if (result != ITL_IWX_RESOURCE_OK) return result;
    cmd.ticket = draft.ticket;
    *out = draft; *command = cmd;
    return ITL_IWX_RESOURCE_OK;
}
static inline int
itl_iwx_drain_command_matches(const struct itl_iwx_drain_session *s,
    const struct itl_iwx_station_request *cmd,uint8_t opcode)
{
    return cmd && itl_iwx_drain_same_ticket(&s->ticket,&cmd->ticket) &&
        cmd->group == 1 && cmd->opcode == opcode &&
        cmd->version == (opcode == 0x18 ? 12 : 2) &&
        cmd->reply_version == (opcode == 0x18 ? 2 : 1) &&
        cmd->length == (opcode == 0x18 ? 48 : 8);
}
static inline enum itl_iwx_awdl_resource_result
itl_iwx_drain_flow_reply(struct itl_iwx_awdl_resources *p,struct itl_iwx_drain_session *s,
    const struct itl_iwx_station_request *command,int transport_ok,int failed,
    const void *bytes,size_t length,struct itl_iwx_station_request *flush)
{
    if (!flush) return ITL_IWX_RESOURCE_INVALID;
    memset(flush,0,sizeof(*flush));
    enum itl_iwx_awdl_resource_result result = itl_iwx_drain_active(p,s);
    if (result != ITL_IWX_RESOURCE_OK) return result;
    if (s->phase != ITL_IWX_DRAIN_FLOW || !itl_iwx_drain_command_matches(s,command,0x18))
        return ITL_IWX_RESOURCE_STALE;
    if (transport_ok != 1 || failed || !bytes || length != 4 || ((const uint8_t *)bytes)[0] != 1)
        return itl_iwx_drain_abort(p,s);
    flush->ticket = s->ticket;
    flush->group = 1; flush->opcode = 0x1e; flush->version = 2; flush->reply_version = 1; flush->length = 8;
    flush->bytes[0] = (uint8_t)s->ticket.handle.station;
    uint16_t mask = (uint16_t)(1U << s->tid);
    flush->bytes[4] = (uint8_t)mask; flush->bytes[5] = (uint8_t)(mask >> 8);
    s->phase = ITL_IWX_DRAIN_FLUSH;
    return ITL_IWX_RESOURCE_OK;
}
static inline enum itl_iwx_awdl_resource_result
itl_iwx_drain_flush_reply(struct itl_iwx_awdl_resources *p,struct itl_iwx_drain_session *s,
    const struct itl_iwx_station_request *command,int transport_ok,int failed,const void *bytes,size_t length)
{
    struct itl_iwx_flush_response reply;
    enum itl_iwx_awdl_resource_result result = itl_iwx_drain_active(p,s);
    if (result != ITL_IWX_RESOURCE_OK) return result;
    if (s->phase != ITL_IWX_DRAIN_FLUSH || !itl_iwx_drain_command_matches(s,command,0x1e))
        return ITL_IWX_RESOURCE_STALE;
    if (transport_ok != 1 || failed || itl_iwx_flush_decode(bytes,length,s->ticket.handle.station,
        (uint16_t)(1U << s->tid),&reply) != ITL_IWX_FLUSH_OK || reply.count != 1 ||
        reply.entries[0].queue != s->queue || reply.entries[0].tid != s->tid)
        return itl_iwx_drain_abort(p,s);
    s->read_before = reply.entries[0].read_before; s->read_after = reply.entries[0].read_after;
    s->phase = ITL_IWX_DRAIN_HOST;
    return ITL_IWX_RESOURCE_OK;
}
/* Host evidence is not a device readback performed by this module. tx_blocked
 * certifies the ENTIRE interval since prepare, not just a current boolean.
 * The backend must establish the sequence modulus/normalization for its actual
 * hardware. A valid snapshot that is still draining may be polled again; unsafe
 * identity/geometry/fence evidence quarantines. No ring or DMA memory is freed.
 */
static inline enum itl_iwx_awdl_resource_result
itl_iwx_drain_confirm_host(struct itl_iwx_awdl_resources *p,struct itl_iwx_drain_session *s,
    const struct itl_iwx_awdl_resource_ticket *ticket,const struct itl_iwx_drain_host *host)
{
    enum itl_iwx_awdl_resource_result result = itl_iwx_drain_active(p,s);
    if (result != ITL_IWX_RESOURCE_OK) return result;
    if (s->phase != ITL_IWX_DRAIN_HOST || !itl_iwx_drain_same_ticket(ticket,&s->ticket))
        return ITL_IWX_RESOURCE_STALE;
    if (!host) return ITL_IWX_RESOURCE_INVALID;
    if (host->epoch != s->ticket.handle.epoch || host->station != s->ticket.handle.station ||
        host->queue != s->queue || host->tid != s->tid || host->tx_blocked != 1 ||
        host->ring_count < 8 || host->ring_count > 65536 || (host->ring_count & (host->ring_count-1)) ||
        host->sequence_modulus < host->ring_count || host->sequence_modulus > 65536 ||
        (host->sequence_modulus & (host->sequence_modulus-1)) ||
        host->producer >= host->sequence_modulus || host->consumer >= host->sequence_modulus ||
        host->queued > host->ring_count || host->dma_quiescent > 1)
        return itl_iwx_drain_abort(p,s);
    if (!host->dma_quiescent || host->queued || host->producer != host->consumer ||
        (s->read_after & (host->sequence_modulus-1)) != host->consumer)
        return ITL_IWX_RESOURCE_STATE;
    result = itl_iwx_awdl_resources_complete(p,&s->ticket,ITL_IWX_RESOURCE_SUCCESS,0);
    if (result == ITL_IWX_RESOURCE_OK) s->phase = ITL_IWX_DRAIN_DONE;
    return result;
}
#endif
