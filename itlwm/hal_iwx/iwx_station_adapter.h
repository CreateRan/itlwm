/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL_IWX_STATION_ADAPTER_H
#define ITL_IWX_STATION_ADAPTER_H
#include "iwx_awdl_resources.h"
enum { ITL_IWX_STATION_ADD_SIZE = 48, ITL_IWX_STATION_REMOVE_SIZE = 4 };
struct itl_iwx_station_versions { uint8_t add_command, add_reply, remove_command, remove_reply; };
struct itl_iwx_station_context { uint32_t id, color, station_type; };
struct itl_iwx_station_request {
    struct itl_iwx_awdl_resource_ticket ticket;
    uint8_t group, opcode, version, reply_version;
    uint16_t length;
    uint8_t bytes[ITL_IWX_STATION_ADD_SIZE];
};
/* Offline command preparation only. Caller serializes access, supplies a
 * validated independent MAC context/station type and versions from the same
 * firmware epoch. A byte-encodable context is not proof firmware supports it.
 * Requests are immutable and per-ledger; output must not alias any input/pool.
 */
static inline enum itl_iwx_awdl_resource_result
itl_iwx_station_prepare(struct itl_iwx_awdl_resources *pool,
    const struct itl_iwx_awdl_resource_handle *handle,
    const struct itl_iwx_station_versions *versions,
    const struct itl_iwx_station_context *context,
    enum itl_iwx_awdl_resource_action action, struct itl_iwx_station_request *out)
{
    struct itl_iwx_station_request draft;
    enum itl_iwx_awdl_resource_result result;
    if (!out)
        return ITL_IWX_RESOURCE_INVALID;
    memset(out, 0, sizeof(*out));
    if (!pool || pool->configured != 1 || !handle || !versions)
        return ITL_IWX_RESOURCE_INVALID;
    int slot = itl_iwx_awdl_resources_find(pool, handle);
    if (slot < 0)
        return ITL_IWX_RESOURCE_STALE;
    memset(&draft, 0, sizeof(draft));
    draft.group = 1;
    if (action == ITL_IWX_RESOURCE_ADD_STATION) {
        if (versions->add_command != 12 || versions->add_reply != 2 || !context ||
            context->id > 255 || context->color > 255 || context->station_type > 4)
            return ITL_IWX_RESOURCE_INVALID;
        draft.opcode = 0x18;
        draft.version = 12;
        draft.reply_version = 2;
        draft.length = ITL_IWX_STATION_ADD_SIZE;
        draft.bytes[2] = 0xff; /* Disable aggregation for all TIDs. */
        draft.bytes[3] = 0xff;
        draft.bytes[4] = (uint8_t)context->id;
        draft.bytes[5] = (uint8_t)context->color;
        memcpy(draft.bytes + 8, pool->entries[slot].peer, 6);
        draft.bytes[16] = (uint8_t)handle->station;
        /* FAT/MIMO mask with zero flags: narrow SISO wire profile only. */
        draft.bytes[27] = 0x3c;
        draft.bytes[35] = (uint8_t)context->station_type;
    } else if (action == ITL_IWX_RESOURCE_REMOVE_STATION) {
        if (versions->remove_command != 2 || versions->remove_reply != 0)
            return ITL_IWX_RESOURCE_INVALID;
        draft.opcode = 0x19;
        draft.version = 2;
        draft.reply_version = 0;
        draft.length = ITL_IWX_STATION_REMOVE_SIZE;
        draft.bytes[0] = (uint8_t)handle->station;
    } else {
        /* A drain-flow flag acknowledgement is not a completed ledger drain. */
        return ITL_IWX_RESOURCE_STATE;
    }
    result = itl_iwx_awdl_resources_begin(pool, handle, action, &draft.ticket);
    if (result != ITL_IWX_RESOURCE_OK)
        return result;
    *out = draft;
    return ITL_IWX_RESOURCE_OK;
}

/* Transport evidence must identify the original immutable request and firmware
 * epoch. Only a successful transport, nonfailed envelope and exact status word
 * can establish ADD success. All other outcomes are conservatively uncertain;
 * an error status alone is not assumed to prove an atomic no-effect rejection.
 */
static inline enum itl_iwx_awdl_resource_result
itl_iwx_station_finish_add(struct itl_iwx_awdl_resources *pool,
    const struct itl_iwx_station_request *request, int transport_ok, int command_failed,
    const void *bytes, size_t length)
{
    enum itl_iwx_awdl_resource_outcome outcome = ITL_IWX_RESOURCE_UNCERTAIN;
    if (!request || request->group != 1 || request->opcode != 0x18 ||
        request->version != 12 || request->reply_version != 2 ||
        request->length != ITL_IWX_STATION_ADD_SIZE ||
        request->ticket.action != ITL_IWX_RESOURCE_ADD_STATION ||
        request->bytes[16] != request->ticket.handle.station)
        return ITL_IWX_RESOURCE_INVALID;
    if (transport_ok == 1 && !command_failed && bytes && length == 4) {
        /* Intel ADD_STA_STATUS_MASK is 0xff; upper bits carry other metadata. */
        const uint8_t *status = (const uint8_t *)bytes;
        if (status[0] == 1)
            outcome = ITL_IWX_RESOURCE_SUCCESS;
    }
    return itl_iwx_awdl_resources_complete(pool, &request->ticket, outcome, 0);
}

/* No structured remove-status decoder is invented for notification version 0.
 * The caller must separately establish station absence. A send return value or
 * an empty payload alone is NOT confirmation. Station-bound queue ownership
 * remains until the ledger's separate RELEASE_HOST_QUEUE lifetime fence.
 */
static inline enum itl_iwx_awdl_resource_result
itl_iwx_station_finish_remove(struct itl_iwx_awdl_resources *pool,
    const struct itl_iwx_station_request *request, int station_absence_confirmed)
{
    if (!request || request->group != 1 || request->opcode != 0x19 ||
        request->version != 2 || request->reply_version != 0 ||
        request->length != ITL_IWX_STATION_REMOVE_SIZE ||
        request->ticket.action != ITL_IWX_RESOURCE_REMOVE_STATION ||
        request->bytes[0] != request->ticket.handle.station)
        return ITL_IWX_RESOURCE_INVALID;
    return itl_iwx_awdl_resources_complete(pool, &request->ticket,
        station_absence_confirmed == 1 ? ITL_IWX_RESOURCE_SUCCESS : ITL_IWX_RESOURCE_UNCERTAIN, 0);
}
#endif
