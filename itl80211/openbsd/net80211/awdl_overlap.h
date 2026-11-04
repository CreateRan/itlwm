/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ITL80211_AWDL_OVERLAP_H
#define ITL80211_AWDL_OVERLAP_H
#include "awdl_sync.h"
enum { ITL_AWDL_OVERLAP_STEP_LIMIT = 128 };
enum itl_awdl_overlap_result {
    ITL_AWDL_OVERLAP_OK, ITL_AWDL_OVERLAP_INVALID, ITL_AWDL_OVERLAP_STALE,
    ITL_AWDL_OVERLAP_UNSUPPORTED, ITL_AWDL_OVERLAP_NONE, ITL_AWDL_OVERLAP_LIMIT
};
struct itl_awdl_observation {
    const struct itl_awdl_sync *sync;
    uint64_t received_us, max_age_us;
};
struct itl_awdl_overlap_policy {
    uint64_t horizon_us;
    uint32_t local_guard_us, peer_guard_us, transaction_us;
};
struct itl_awdl_overlap {
    uint64_t wait_us, usable_us;
    uint16_t local_aw_counter, peer_aw_counter;
    uint8_t channel, operating_class;
};
static inline uint64_t itl_awdl_overlap_min(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

/* Pure nominal overlap search; immutable caller-validated inputs and separate
 * output storage. Identical nonzero operating-class/channel bytes are only a
 * candidate identity match, not regulatory/firmware permission to transmit.
 * Guards are caller-provided microseconds, not the raw discovery guard field.
 */
static inline enum itl_awdl_overlap_result
itl_awdl_find_overlap(const struct itl_awdl_observation *local,
    const struct itl_awdl_observation *peer, uint64_t now,
    const struct itl_awdl_overlap_policy *policy, struct itl_awdl_overlap *out)
{
    struct itl_awdl_overlap candidate = {0};
    struct itl_awdl_window lw, pw;
    enum itl_awdl_result lr, pr;
    uint64_t local_age, peer_age, horizon, local_aw_us, peer_aw_us, cursor = 0;
    if (!out) return ITL_AWDL_OVERLAP_INVALID;
    memset(out,0,sizeof(*out));
    if (!local || !peer || !local->sync || !peer->sync || !policy || !policy->transaction_us)
        return ITL_AWDL_OVERLAP_INVALID;
    if (now < local->received_us || now < peer->received_us) return ITL_AWDL_OVERLAP_INVALID;
    local_age = now - local->received_us; peer_age = now - peer->received_us;
    if (local_age > local->max_age_us || peer_age > peer->max_age_us) return ITL_AWDL_OVERLAP_STALE;
    lr = itl_awdl_project_window(local->sync,local->received_us,now,local->max_age_us,&lw);
    pr = itl_awdl_project_window(peer->sync,peer->received_us,now,peer->max_age_us,&pw);
    if (lr == ITL_AWDL_MALFORMED || pr == ITL_AWDL_MALFORMED) return ITL_AWDL_OVERLAP_INVALID;
    if (lr != ITL_AWDL_VALID || pr != ITL_AWDL_VALID || lw.encoding != 3 || pw.encoding != 3)
        return ITL_AWDL_OVERLAP_UNSUPPORTED;

    horizon = itl_awdl_overlap_min(policy->horizon_us,UINT64_MAX-now);
    horizon = itl_awdl_overlap_min(horizon,local->max_age_us-local_age);
    horizon = itl_awdl_overlap_min(horizon,peer->max_age_us-peer_age);
    local_aw_us = (uint64_t)local->sync->aw_period_tu * 1024;
    peer_aw_us = (uint64_t)peer->sync->aw_period_tu * 1024;
    // Conservative profile: a transaction must fit one guarded AW on each side.
    if (2*(uint64_t)policy->local_guard_us >= local_aw_us ||
        2*(uint64_t)policy->peer_guard_us >= peer_aw_us) return ITL_AWDL_OVERLAP_NONE;
    if (policy->transaction_us > local_aw_us-2*(uint64_t)policy->local_guard_us ||
        policy->transaction_us > peer_aw_us-2*(uint64_t)policy->peer_guard_us ||
        policy->transaction_us > horizon) return ITL_AWDL_OVERLAP_NONE;

    for (unsigned step=0;step<ITL_AWDL_OVERLAP_STEP_LIMIT && cursor<horizon;++step) {
        uint64_t start = 0, end, segment, elapsed_l, elapsed_p;
        // cursor is bounded by horizon <= UINT64_MAX-now and both age limits.
        lr = itl_awdl_project_window(local->sync,local->received_us,now+cursor,local->max_age_us,&lw);
        pr = itl_awdl_project_window(peer->sync,peer->received_us,now+cursor,peer->max_age_us,&pw);
        if (lr != ITL_AWDL_VALID || pr != ITL_AWDL_VALID) return ITL_AWDL_OVERLAP_INVALID;
        segment = itl_awdl_overlap_min(lw.to_next_aw_us,pw.to_next_aw_us);
        segment = itl_awdl_overlap_min(segment,horizon-cursor);
        if (!segment) return ITL_AWDL_OVERLAP_INVALID;
        elapsed_l = local_aw_us-lw.to_next_aw_us; elapsed_p = peer_aw_us-pw.to_next_aw_us;
        if (elapsed_l < policy->local_guard_us) start = policy->local_guard_us-elapsed_l;
        if (elapsed_p < policy->peer_guard_us && policy->peer_guard_us-elapsed_p > start)
            start = policy->peer_guard_us-elapsed_p;
        end = itl_awdl_overlap_min(
            lw.to_next_aw_us > policy->local_guard_us ? lw.to_next_aw_us-policy->local_guard_us : 0,
            pw.to_next_aw_us > policy->peer_guard_us ? pw.to_next_aw_us-policy->peer_guard_us : 0);
        end = itl_awdl_overlap_min(end,horizon-cursor);
        if (lw.channel && lw.auxiliary && lw.channel==pw.channel && lw.auxiliary==pw.auxiliary &&
            end>=start && end-start>=policy->transaction_us) {
            candidate.wait_us = cursor+start; candidate.usable_us = end-start;
            candidate.local_aw_counter = lw.aw_counter; candidate.peer_aw_counter = pw.aw_counter;
            candidate.channel = lw.channel; candidate.operating_class = lw.auxiliary;
            *out = candidate;
            return ITL_AWDL_OVERLAP_OK;
        }
        cursor += segment;
    }
    return cursor < horizon ? ITL_AWDL_OVERLAP_LIMIT : ITL_AWDL_OVERLAP_NONE;
}
#endif
