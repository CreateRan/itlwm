/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL_IWX_AWDL_RESOURCES_H
#define ITL_IWX_AWDL_RESOURCES_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum { ITL_IWX_AWDL_STATIONS = 16, ITL_IWX_AWDL_QUEUES = 512 };
enum itl_iwx_awdl_resource_result {
    ITL_IWX_RESOURCE_OK, ITL_IWX_RESOURCE_INVALID, ITL_IWX_RESOURCE_FULL,
    ITL_IWX_RESOURCE_DUPLICATE, ITL_IWX_RESOURCE_STALE, ITL_IWX_RESOURCE_STATE,
    ITL_IWX_RESOURCE_CONFLICT, ITL_IWX_RESOURCE_FAULT, ITL_IWX_RESOURCE_EXHAUSTED,
    ITL_IWX_RESOURCE_REJECTED, ITL_IWX_RESOURCE_UNKNOWN
};
enum itl_iwx_awdl_resource_phase {
    ITL_IWX_RESOURCE_FREE, ITL_IWX_RESOURCE_RESERVED, ITL_IWX_RESOURCE_STATION,
    ITL_IWX_RESOURCE_READY, ITL_IWX_RESOURCE_QUEUE_IDLE,
    ITL_IWX_RESOURCE_PENDING, ITL_IWX_RESOURCE_QUARANTINED, ITL_IWX_RESOURCE_STATION_GONE
};
enum itl_iwx_awdl_resource_action {
    ITL_IWX_RESOURCE_ADD_STATION = 1, ITL_IWX_RESOURCE_ALLOC_QUEUE,
    ITL_IWX_RESOURCE_DRAIN_QUEUE, ITL_IWX_RESOURCE_REMOVE_QUEUE,
    ITL_IWX_RESOURCE_REMOVE_STATION, ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE
};
enum itl_iwx_awdl_resource_outcome {
    ITL_IWX_RESOURCE_SUCCESS, ITL_IWX_RESOURCE_DENIED, ITL_IWX_RESOURCE_UNCERTAIN
};
enum itl_iwx_awdl_queue_lifetime {
    ITL_IWX_RESOURCE_LIFETIME_UNKNOWN,
    ITL_IWX_RESOURCE_SEPARATE_REMOVE, ITL_IWX_RESOURCE_STATION_BOUND
};
struct itl_iwx_awdl_resource_config {
    uint8_t queue_lifetime;
    uint16_t station_count, queue_count, station_mask;
    uint8_t queue_mask[ITL_IWX_AWDL_QUEUES / 8];
};
struct itl_iwx_awdl_resource_handle {
    uint64_t epoch, lease;
    uint16_t station;
};
struct itl_iwx_awdl_resource_ticket {
    struct itl_iwx_awdl_resource_handle handle;
    uint64_t serial;
    uint8_t action;
};
struct itl_iwx_awdl_resource_entry {
    uint64_t lease, operation;
    uint8_t peer[6], closing, has_queue, phase, action;
    uint16_t queue;
};
struct itl_iwx_awdl_resources {
    uint8_t configured, fault;
    uint64_t epoch, serial;
    struct itl_iwx_awdl_resource_config config;
    struct itl_iwx_awdl_resource_entry entries[ITL_IWX_AWDL_STATIONS];
};
/* Caller owns and serializes this ledger. No firmware, DMA or TX operations.
 * Grants must be exclusive against ALL other station/queue allocators, even
 * before IDs are assigned here. Init is only for fresh/fenced storage; use
 * reset after a confirmed firmware/DMA reset. Handles are per-ledger.
 * Output pointers/tickets/handles must not overlap ledger or other arguments.
 */
static inline enum itl_iwx_awdl_resource_result
itl_iwx_awdl_resources_init(struct itl_iwx_awdl_resources *p,
    const struct itl_iwx_awdl_resource_config *c, uint64_t epoch)
{
    if (!p || !c || !epoch || c->station_count < 3 || c->station_count > 16 ||
        c->queue_count < 10 || c->queue_count > 512 ||
        (c->station_mask & 7) || (c->station_mask >> c->station_count) ||
        (c->queue_lifetime != ITL_IWX_RESOURCE_SEPARATE_REMOVE &&
         c->queue_lifetime != ITL_IWX_RESOURCE_STATION_BOUND))
        return ITL_IWX_RESOURCE_INVALID;
    for (unsigned q = 0; q < ITL_IWX_AWDL_QUEUES; ++q)
        if ((c->queue_mask[q / 8] & (1U << (q % 8))) &&
            (q < 10 || q >= c->queue_count))
            return ITL_IWX_RESOURCE_INVALID;
    const struct itl_iwx_awdl_resource_config config = *c;
    memset(p, 0, sizeof(*p));
    p->config = config;
    p->epoch = epoch;
    p->configured = 1;
    return ITL_IWX_RESOURCE_OK;
}

/* The caller must first fence old commands, stop DMA and reset firmware.
 * A host sleep/wake notification or transport generation change is not proof.
 */
static inline enum itl_iwx_awdl_resource_result
itl_iwx_awdl_resources_reset(struct itl_iwx_awdl_resources *p, uint64_t epoch)
{
    if (!p || p->configured != 1 || epoch <= p->epoch)
        return ITL_IWX_RESOURCE_INVALID;
    memset(p->entries, 0, sizeof(p->entries));
    p->epoch = epoch;
    p->serial = 0;
    p->fault = 0;
    return ITL_IWX_RESOURCE_OK;
}

static inline int
itl_iwx_awdl_resources_find(const struct itl_iwx_awdl_resources *p,
    const struct itl_iwx_awdl_resource_handle *h)
{
    if (!p || p->configured != 1 || !h || !h->lease ||
        h->epoch != p->epoch || h->station >= ITL_IWX_AWDL_STATIONS)
        return -1;
    const struct itl_iwx_awdl_resource_entry *e = &p->entries[h->station];
    return e->phase != ITL_IWX_RESOURCE_FREE && e->lease == h->lease ?
        (int)h->station : -1;
}

static inline enum itl_iwx_awdl_resource_result
itl_iwx_awdl_resources_reserve(struct itl_iwx_awdl_resources *p, const uint8_t *peer,
    struct itl_iwx_awdl_resource_handle *out)
{
    uint8_t bits = 0;
    int free_station = -1;
    if (!out)
        return ITL_IWX_RESOURCE_INVALID;
    memset(out, 0, sizeof(*out));
    if (!p || p->configured != 1 || !peer || (peer[0] & 1))
        return ITL_IWX_RESOURCE_INVALID;
    for (unsigned i = 0; i < 6; ++i)
        bits |= peer[i];
    if (!bits)
        return ITL_IWX_RESOURCE_INVALID;
    if (p->fault)
        return ITL_IWX_RESOURCE_FAULT;
    for (unsigned i = 3; i < p->config.station_count; ++i) {
        const struct itl_iwx_awdl_resource_entry *e = &p->entries[i];
        if (e->phase != ITL_IWX_RESOURCE_FREE && !memcmp(e->peer, peer, 6))
            return ITL_IWX_RESOURCE_DUPLICATE;
        if (free_station < 0 && e->phase == ITL_IWX_RESOURCE_FREE &&
            (p->config.station_mask & (1U << i)))
            free_station = (int)i;
    }
    if (p->serial == UINT64_MAX)
        return ITL_IWX_RESOURCE_EXHAUSTED;
    if (free_station < 0)
        return ITL_IWX_RESOURCE_FULL;
    struct itl_iwx_awdl_resource_entry *e = &p->entries[free_station];
    memcpy(e->peer, peer, 6);
    e->lease = ++p->serial;
    e->phase = ITL_IWX_RESOURCE_RESERVED;
    out->epoch = p->epoch;
    out->lease = e->lease;
    out->station = (uint16_t)free_station;
    return ITL_IWX_RESOURCE_OK;
}

static inline enum itl_iwx_awdl_resource_result
itl_iwx_awdl_resources_cancel(struct itl_iwx_awdl_resources *p,
    const struct itl_iwx_awdl_resource_handle *h)
{
    if (!p || p->configured != 1 || !h)
        return ITL_IWX_RESOURCE_INVALID;
    int i = itl_iwx_awdl_resources_find(p, h);
    if (i < 0)
        return ITL_IWX_RESOURCE_STALE;
    if (p->entries[i].phase != ITL_IWX_RESOURCE_RESERVED)
        return ITL_IWX_RESOURCE_STATE;
    memset(&p->entries[i], 0, sizeof(p->entries[i]));
    return ITL_IWX_RESOURCE_OK;
}

static inline enum itl_iwx_awdl_resource_result
itl_iwx_awdl_resources_close(struct itl_iwx_awdl_resources *p,
    const struct itl_iwx_awdl_resource_handle *h)
{
    if (!p || p->configured != 1 || !h)
        return ITL_IWX_RESOURCE_INVALID;
    int i = itl_iwx_awdl_resources_find(p, h);
    if (i < 0)
        return ITL_IWX_RESOURCE_STALE;
    p->entries[i].closing = 1;
    return ITL_IWX_RESOURCE_OK;
}

/* Call before command submission. Only one pending operation per lease.
 * A begin success grants no hardware ownership; it records expected intent.
 */
static inline enum itl_iwx_awdl_resource_result
itl_iwx_awdl_resources_begin(struct itl_iwx_awdl_resources *p,
    const struct itl_iwx_awdl_resource_handle *h, enum itl_iwx_awdl_resource_action a,
    struct itl_iwx_awdl_resource_ticket *out)
{
    if (!out)
        return ITL_IWX_RESOURCE_INVALID;
    memset(out, 0, sizeof(*out));
    if (!p || p->configured != 1 || !h ||
        a < ITL_IWX_RESOURCE_ADD_STATION || a > ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE)
        return ITL_IWX_RESOURCE_INVALID;
    int i = itl_iwx_awdl_resources_find(p, h);
    if (i < 0)
        return ITL_IWX_RESOURCE_STALE;
    struct itl_iwx_awdl_resource_entry *e = &p->entries[i];
    const int creation = a == ITL_IWX_RESOURCE_ADD_STATION || a == ITL_IWX_RESOURCE_ALLOC_QUEUE;
    if (creation && p->fault)
        return ITL_IWX_RESOURCE_FAULT;
    if (creation && e->closing)
        return ITL_IWX_RESOURCE_STATE;
    if ((a == ITL_IWX_RESOURCE_REMOVE_QUEUE &&
         p->config.queue_lifetime != ITL_IWX_RESOURCE_SEPARATE_REMOVE) ||
        (a == ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE &&
         p->config.queue_lifetime != ITL_IWX_RESOURCE_STATION_BOUND))
        return ITL_IWX_RESOURCE_STATE;
    uint8_t expected = ITL_IWX_RESOURCE_RESERVED;
    switch (a) {
    case ITL_IWX_RESOURCE_ADD_STATION: expected = ITL_IWX_RESOURCE_RESERVED; break;
    case ITL_IWX_RESOURCE_ALLOC_QUEUE: expected = ITL_IWX_RESOURCE_STATION; break;
    case ITL_IWX_RESOURCE_REMOVE_STATION:
        expected = p->config.queue_lifetime == ITL_IWX_RESOURCE_STATION_BOUND && e->has_queue ?
            ITL_IWX_RESOURCE_QUEUE_IDLE : ITL_IWX_RESOURCE_STATION;
        break;
    case ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE: expected = ITL_IWX_RESOURCE_STATION_GONE; break;
    case ITL_IWX_RESOURCE_DRAIN_QUEUE: expected = ITL_IWX_RESOURCE_READY; break;
    case ITL_IWX_RESOURCE_REMOVE_QUEUE: expected = ITL_IWX_RESOURCE_QUEUE_IDLE; break;
    }
    if (e->phase != expected)
        return ITL_IWX_RESOURCE_STATE;
    if (a == ITL_IWX_RESOURCE_ALLOC_QUEUE) {
        unsigned grants = 0, committed = 0;
        for (unsigned q = 10; q < p->config.queue_count; ++q)
            if (p->config.queue_mask[q / 8] & (1U << (q % 8)))
                ++grants;
        for (unsigned n = 3; n < p->config.station_count; ++n) {
            const struct itl_iwx_awdl_resource_entry *owner = &p->entries[n];
            if (owner->has_queue || (owner->phase == ITL_IWX_RESOURCE_PENDING &&
                owner->action == ITL_IWX_RESOURCE_ALLOC_QUEUE))
                ++committed;
        }
        if (committed >= grants)
            return ITL_IWX_RESOURCE_FULL;
    }
    if (p->serial == UINT64_MAX)
        return ITL_IWX_RESOURCE_EXHAUSTED;
    e->operation = ++p->serial;
    e->action = (uint8_t)a;
    e->phase = ITL_IWX_RESOURCE_PENDING;
    if (!creation)
        e->closing = 1;
    out->handle = *h;
    out->serial = e->operation;
    out->action = (uint8_t)a;
    return ITL_IWX_RESOURCE_OK;
}

/* DENIED is valid only for a definitive no-effect result. A timeout, malformed
 * reply or transport error after possible submission is UNCERTAIN. SUCCESS for
 * DRAIN/REMOVE_QUEUE requires actual firmware completion and DMA quiescence,
 * not merely sending a command or clearing a host-side ring. In station-bound
 * mode, REMOVE_STATION confirms station absence only and retains queue/lease.
 * RELEASE_HOST_QUEUE is a host lifetime fence, not a firmware command: success
 * additionally certifies no possible device DMA and completed host cleanup.
 */
static inline enum itl_iwx_awdl_resource_result
itl_iwx_awdl_resources_complete(struct itl_iwx_awdl_resources *p,
    const struct itl_iwx_awdl_resource_ticket *t, enum itl_iwx_awdl_resource_outcome result,
    uint32_t queue)
{
    if (!p || p->configured != 1 || !t ||
        result < ITL_IWX_RESOURCE_SUCCESS || result > ITL_IWX_RESOURCE_UNCERTAIN)
        return ITL_IWX_RESOURCE_INVALID;
    int i = itl_iwx_awdl_resources_find(p, &t->handle);
    if (i < 0)
        return ITL_IWX_RESOURCE_STALE;
    struct itl_iwx_awdl_resource_entry *e = &p->entries[i];
    if (e->phase != ITL_IWX_RESOURCE_PENDING || !t->serial ||
        e->operation != t->serial || e->action != t->action)
        return ITL_IWX_RESOURCE_STALE;
    const uint8_t action = e->action;
    e->operation = 0;
    e->action = 0;
    if (result == ITL_IWX_RESOURCE_UNCERTAIN) {
        e->phase = ITL_IWX_RESOURCE_QUARANTINED;
        e->closing = 1;
        if (action == ITL_IWX_RESOURCE_ALLOC_QUEUE)
            p->fault = 1; /* Unknown returned queue could alias any owner. */
        return ITL_IWX_RESOURCE_UNKNOWN;
    }
    if (result == ITL_IWX_RESOURCE_DENIED) {
        switch (action) {
        case ITL_IWX_RESOURCE_ADD_STATION: memset(e, 0, sizeof(*e)); break;
        case ITL_IWX_RESOURCE_ALLOC_QUEUE: e->phase = ITL_IWX_RESOURCE_STATION; break;
        case ITL_IWX_RESOURCE_REMOVE_STATION:
            e->phase = p->config.queue_lifetime == ITL_IWX_RESOURCE_STATION_BOUND && e->has_queue ?
                ITL_IWX_RESOURCE_QUEUE_IDLE : ITL_IWX_RESOURCE_STATION;
            break;
        case ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE: e->phase = ITL_IWX_RESOURCE_STATION_GONE; break;
        case ITL_IWX_RESOURCE_DRAIN_QUEUE: e->phase = ITL_IWX_RESOURCE_READY; break;
        case ITL_IWX_RESOURCE_REMOVE_QUEUE: e->phase = ITL_IWX_RESOURCE_QUEUE_IDLE; break;
        }
        return ITL_IWX_RESOURCE_REJECTED;
    }
    switch (action) {
    case ITL_IWX_RESOURCE_ADD_STATION:
        e->phase = ITL_IWX_RESOURCE_STATION;
        break;
    case ITL_IWX_RESOURCE_ALLOC_QUEUE: {
        int conflict = queue < 10 || queue >= p->config.queue_count;
        if (!conflict && !(p->config.queue_mask[queue / 8] & (1U << (queue % 8))))
            conflict = 1;
        for (unsigned n = 3; n < p->config.station_count; ++n)
            if (p->entries[n].has_queue && p->entries[n].queue == queue)
                conflict = 1;
        if (conflict) {
            p->fault = 1;
            e->closing = 1;
            e->phase = ITL_IWX_RESOURCE_QUARANTINED;
            return ITL_IWX_RESOURCE_CONFLICT;
        }
        e->queue = (uint16_t)queue;
        e->has_queue = 1;
        e->phase = ITL_IWX_RESOURCE_READY;
        break;
    }
    case ITL_IWX_RESOURCE_DRAIN_QUEUE:
        e->phase = ITL_IWX_RESOURCE_QUEUE_IDLE;
        break;
    case ITL_IWX_RESOURCE_REMOVE_QUEUE:
        e->queue = 0;
        e->has_queue = 0;
        e->phase = ITL_IWX_RESOURCE_STATION;
        break;
    case ITL_IWX_RESOURCE_REMOVE_STATION:
        if (e->has_queue && p->config.queue_lifetime == ITL_IWX_RESOURCE_STATION_BOUND)
            e->phase = ITL_IWX_RESOURCE_STATION_GONE;
        else
            memset(e, 0, sizeof(*e));
        break;
    case ITL_IWX_RESOURCE_RELEASE_HOST_QUEUE:
        memset(e, 0, sizeof(*e));
        break;
    }
    return ITL_IWX_RESOURCE_OK;
}

/* A policy lookup, not a DMA pin or permission to transmit later. The adapter
 * must revalidate in the same serialized operation that commits real TX.
 */
static inline enum itl_iwx_awdl_resource_result
itl_iwx_awdl_resources_tx_queue(const struct itl_iwx_awdl_resources *p,
    const struct itl_iwx_awdl_resource_handle *h, uint16_t *out)
{
    if (!out)
        return ITL_IWX_RESOURCE_INVALID;
    *out = 0;
    if (!p || p->configured != 1 || !h)
        return ITL_IWX_RESOURCE_INVALID;
    int i = itl_iwx_awdl_resources_find(p, h);
    if (i < 0)
        return ITL_IWX_RESOURCE_STALE;
    if (p->fault)
        return ITL_IWX_RESOURCE_FAULT;
    const struct itl_iwx_awdl_resource_entry *e = &p->entries[i];
    if (e->phase != ITL_IWX_RESOURCE_READY || e->closing || !e->has_queue)
        return ITL_IWX_RESOURCE_STATE;
    *out = e->queue;
    return ITL_IWX_RESOURCE_OK;
}
#endif
