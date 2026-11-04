/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "AirportItlwm/AWDL/InterfaceLifecycle.hpp"
using AirportAWDL::InterfaceLifecycle;
static void test_failed_creation_can_retry()
{
    InterfaceLifecycle state;
    assert(!state.accepting() && !state.beginCreate());
    assert(state.canReleaseDependencies());
    assert(state.open());
    assert(!state.canReleaseDependencies());
    assert(state.beginCreate());
    assert(!state.beginCreate());
    assert(state.completeCreate(false));
    assert(!state.ownsChild());
    assert(state.beginCreate());
    assert(state.completeCreate(true));
    assert(state.ownsChild() && !state.beginCreate());
    assert(!state.open());
}
static void test_termination_refusal_keeps_dependencies()
{
    InterfaceLifecycle state;
    assert(state.open() && state.beginCreate() && state.completeCreate(true));
    state.quiesce();
    assert(!state.accepting() && !state.beginCreate());
    // A refused/pending termination is not a detach event.
    assert(state.ownsChild() && !state.canReleaseDependencies());
    state.quiesce();
    assert(state.ownsChild() && !state.canReleaseDependencies());
    state.childDetached(); // Only a framework detach permits dependency teardown.
    assert(!state.ownsChild() && state.canReleaseDependencies());
    state.childDetached(); // Cleanup is idempotent.
    assert(state.canReleaseDependencies());
}
static void test_quiesce_during_creation_preserves_late_child()
{
    InterfaceLifecycle state;
    assert(state.open() && state.beginCreate());
    state.quiesce();
    assert(!state.canReleaseDependencies());
    assert(state.completeCreate(true));
    assert(state.ownsChild() && !state.accepting());
    assert(!state.beginCreate() && !state.canReleaseDependencies());
    state.childDetached();
    assert(state.canReleaseDependencies());
    assert(!state.completeCreate(true)); // No phantom owner from a late duplicate.
}
static void test_detach_while_running_allows_recreation()
{
    InterfaceLifecycle state;
    assert(state.open() && state.beginCreate() && state.completeCreate(true));
    state.childDetached();
    assert(state.accepting() && state.beginCreate());
    assert(state.completeCreate(false));
    state.quiesce();
    assert(state.canReleaseDependencies());
}
int main()
{
    test_failed_creation_can_retry();
    test_termination_refusal_keeps_dependencies();
    test_quiesce_during_creation_preserves_late_child();
    test_detach_while_running_allows_recreation();
    puts("AWDL lifecycle tests passed");
}
