/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "itl80211/openbsd/net80211/awdl_overlap.h"
static struct itl_awdl_sync make_sync(uint8_t channel, uint8_t opclass)
{
    struct itl_awdl_sync s; memset(&s,0,sizeof(s));
    s.has_sync=s.has_sequence=1; s.presence_mode=4;
    s.aw_period_tu=16; s.tx_down_counter_tu=64; s.sequence.encoding=3;
    for (unsigned i=0;i<16;++i) { s.sequence.channel[i]=channel; s.sequence.auxiliary[i]=opclass; }
    return s;
}
static void test_guards_exact_fit_and_phase_offset(void)
{
    struct itl_awdl_sync l=make_sync(149,128), p=make_sync(149,128);
    struct itl_awdl_observation lo={&l,0,1000000}, po={&p,0,1000000};
    struct itl_awdl_overlap_policy policy={200000,1000,1000,100};
    struct itl_awdl_overlap out;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    assert(out.wait_us==1000 && out.usable_us==14384 && out.channel==149 && out.operating_class==128);
    assert(itl_awdl_find_overlap(&lo,&po,2000,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    assert(out.wait_us==0 && out.usable_us==13384);
    policy.transaction_us=14384;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    policy.transaction_us=14385;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    policy.transaction_us=100; policy.peer_guard_us=2000;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    assert(out.wait_us==2000 && out.usable_us==12384);
    policy.peer_guard_us=1000; p.tx_down_counter_tu=56;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    assert(out.wait_us==1000 && out.usable_us==6192);
    assert(itl_awdl_find_overlap(&lo,&po,8000,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    assert(out.wait_us==1192 && out.usable_us==6192 && out.local_aw_counter==0 && out.peer_aw_counter==1);
    policy.horizon_us=500;
    assert(itl_awdl_find_overlap(&lo,&po,8000,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
}
static void test_horizon_exact_fit_different_periods_and_future_expiry(void)
{
    struct itl_awdl_sync l=make_sync(149,128), p=make_sync(149,128);
    struct itl_awdl_observation lo={&l,0,1000000}, po={&p,0,1000000};
    struct itl_awdl_overlap_policy policy={1100,1000,1000,100};
    struct itl_awdl_overlap out;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    assert(out.wait_us==1000 && out.usable_us==100);
    policy.horizon_us=1099;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    policy.horizon_us=100000; p.aw_period_tu=32; p.tx_down_counter_tu=128;
    policy.transaction_us=384;
    assert(itl_awdl_find_overlap(&lo,&po,15000,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    assert(out.wait_us==0 && out.usable_us==384);
    policy.transaction_us=385;
    assert(itl_awdl_find_overlap(&lo,&po,15000,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    assert(out.wait_us==2384 && out.usable_us==14384);
    p=make_sync(6,81); p.sequence.channel[1]=149; p.sequence.auxiliary[1]=128;
    policy.local_guard_us=policy.peer_guard_us=0; policy.transaction_us=1; po.max_age_us=65535;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    po.max_age_us=65537;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    assert(out.wait_us==65536 && out.usable_us==1);
    policy.transaction_us=2;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
}
static void test_channel_class_idle_and_counter_wrap(void)
{
    struct itl_awdl_sync l=make_sync(149,128), p=make_sync(149,129);
    struct itl_awdl_observation lo={&l,0,1000000}, po={&p,0,1000000};
    struct itl_awdl_overlap_policy policy={100000,100,100,100};
    struct itl_awdl_overlap out, zero; memset(&zero,0,sizeof(zero));
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    assert(memcmp(&out,&zero,sizeof(out))==0);
    p=make_sync(6,81);
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    p=make_sync(149,128); p.sequence.encoding=0;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_UNSUPPORTED);
    p=make_sync(149,128); l.aw_counter=p.aw_counter=65532;
    l.sequence.channel[15]=p.sequence.channel[15]=0;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_OK);
    assert(out.wait_us==65636 && out.usable_us==16184 && out.local_aw_counter==0 && out.peer_aw_counter==0);
    l.tx_down_counter_tu=p.tx_down_counter_tu=16;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_OK && out.wait_us==16484);
    l=make_sync(0,0); p=make_sync(0,0);
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    l=make_sync(149,0); p=make_sync(149,0);
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
}
static void test_freshness_horizon_overflow_and_budget(void)
{
    struct itl_awdl_sync l=make_sync(149,128), p=make_sync(149,128);
    struct itl_awdl_observation lo={&l,0,50}, po={&p,0,50};
    struct itl_awdl_overlap_policy policy={100000,0,0,50}; struct itl_awdl_overlap out;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_OK && out.usable_us==50);
    policy.transaction_us=51;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    assert(itl_awdl_find_overlap(&lo,&po,51,&policy,&out)==ITL_AWDL_OVERLAP_STALE);
    assert(itl_awdl_find_overlap(&lo,&po,50,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    lo.received_us=1;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_INVALID);
    lo.received_us=po.received_us=UINT64_MAX-100;
    lo.max_age_us=po.max_age_us=UINT64_MAX; policy.horizon_us=UINT64_MAX; policy.transaction_us=100;
    assert(itl_awdl_find_overlap(&lo,&po,UINT64_MAX-100,&policy,&out)==ITL_AWDL_OVERLAP_OK && out.usable_us==100);
    assert(itl_awdl_find_overlap(&lo,&po,UINT64_MAX,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    lo.received_us=po.received_us=0; p=make_sync(6,81); policy.transaction_us=1;
    memset(&out,0xa5,sizeof(out));
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_LIMIT);
    struct itl_awdl_overlap zero; memset(&zero,0,sizeof(zero));
    assert(memcmp(&out,&zero,sizeof(out))==0);
    policy.horizon_us=2097152; // Exactly 128 aligned 16-TU segments.
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    policy.horizon_us=2097153;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_LIMIT);
    policy.local_guard_us=UINT32_MAX;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_NONE);
    policy.local_guard_us=0; policy.transaction_us=0;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_INVALID);
    assert(itl_awdl_find_overlap(NULL,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_INVALID);
    assert(itl_awdl_find_overlap(&lo,&po,0,NULL,&out)==ITL_AWDL_OVERLAP_INVALID);
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,NULL)==ITL_AWDL_OVERLAP_INVALID);
    policy.transaction_us=1; l.has_sync=0;
    memset(&out,0xa5,sizeof(out));
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_UNSUPPORTED);
    assert(memcmp(&out,&zero,sizeof(out))==0);
    l.has_sync=1; l.aw_period_tu=0;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_INVALID);
    assert(memcmp(&out,&zero,sizeof(out))==0);
    l=make_sync(149,128); po.sync=NULL;
    assert(itl_awdl_find_overlap(&lo,&po,0,&policy,&out)==ITL_AWDL_OVERLAP_INVALID);
    assert(memcmp(&out,&zero,sizeof(out))==0);
}
/* Independent per-microsecond oracle for small 1-TU, counter-zero models.
 * It does not call production projection or overlap helpers.
 */
static int oracle_fits(const struct itl_awdl_observation *l, const struct itl_awdl_observation *p,
                      uint64_t at, uint32_t lg, uint32_t pg, uint32_t tx)
{
    uint64_t lp=at-l->received_us, pp=at-p->received_us;
    unsigned li=(unsigned)((lp/4096)%16), pi=(unsigned)((pp/4096)%16);
    uint64_t la=lp%1024, pa=pp%1024;
    return l->sync->sequence.channel[li]!=0 && l->sync->sequence.auxiliary[li]!=0 &&
        l->sync->sequence.channel[li]==p->sync->sequence.channel[pi] &&
        l->sync->sequence.auxiliary[li]==p->sync->sequence.auxiliary[pi] &&
        la>=lg && pa>=pg && la+tx<=1024-lg && pa+tx<=1024-pg;
}
static void test_against_independent_small_time_oracle(void)
{
    uint32_t seed=0x137495aa;
    for (unsigned run=0;run<1000;++run) {
        struct itl_awdl_sync l=make_sync(149,128), p=make_sync(149,128);
        l.aw_period_tu=p.aw_period_tu=1; l.tx_down_counter_tu=p.tx_down_counter_tu=4;
        for (unsigned i=0;i<16;++i) {
            seed=seed*1664525U+1013904223U;
            l.sequence.channel[i]=((seed>>24)&1)?149:6; l.sequence.auxiliary[i]=((seed>>24)&1)?128:81;
            p.sequence.channel[i]=((seed>>25)&1)?149:6; p.sequence.auxiliary[i]=((seed>>25)&1)?128:81;
        }
        seed=seed*1664525U+1013904223U;
        uint64_t now=100+(seed%400);
        struct itl_awdl_observation lo={&l,0,10000}, po={&p,seed%100,10000};
        struct itl_awdl_overlap_policy policy={4000,seed%100,(seed>>8)%100,1+(seed>>16)%100};
        int expected=-1;
        for (unsigned wait=0;wait+policy.transaction_us<=4000;++wait)
            if (oracle_fits(&lo,&po,now+wait,policy.local_guard_us,policy.peer_guard_us,policy.transaction_us))
                { expected=(int)wait; break; }
        struct itl_awdl_overlap out;
        enum itl_awdl_overlap_result result=itl_awdl_find_overlap(&lo,&po,now,&policy,&out);
        if (expected<0) assert(result==ITL_AWDL_OVERLAP_NONE);
        else {
            assert(result==ITL_AWDL_OVERLAP_OK && out.wait_us==(unsigned)expected);
            assert(out.usable_us>=policy.transaction_us && out.wait_us+out.usable_us<=4000);
            uint64_t at=now+(unsigned)expected, available=0;
            uint64_t local_bin=(at-lo.received_us)/1024, peer_bin=(at-po.received_us)/1024;
            while ((unsigned)expected+available<4000) {
                uint64_t point=at+available;
                if ((point-lo.received_us)/1024!=local_bin || (point-po.received_us)/1024!=peer_bin ||
                    !oracle_fits(&lo,&po,point,policy.local_guard_us,policy.peer_guard_us,1)) break;
                ++available;
            }
            assert(out.usable_us==available);
            assert(oracle_fits(&lo,&po,now+out.wait_us,policy.local_guard_us,policy.peer_guard_us,policy.transaction_us));
        }
    }
}
int main(void)
{
    test_guards_exact_fit_and_phase_offset();
    test_horizon_exact_fit_different_periods_and_future_expiry();
    test_channel_class_idle_and_counter_wrap();
    test_freshness_horizon_overflow_and_budget(); test_against_independent_small_time_oracle();
    puts("AWDL overlap planner tests passed");
}
