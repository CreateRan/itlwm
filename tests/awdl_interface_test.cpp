/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "AirportItlwm/AWDL/InterfacePolicy.hpp"
using AirportAWDL::FactoryPlan;
using AirportAWDL::planFactory;
int main()
{
    const uint8_t mac[] = {0x02,0x11,0x22,0x33,0x44,0x55};
    const uint8_t globalMAC[] = {0x00,0x11,0x22,0x33,0x44,0x55};
    const uint8_t multicast[] = {0x01,0,0,0,0,1};
    const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
    const uint8_t zero[6] = {};
    assert(planFactory(true, 6, mac) == FactoryPlan::Create);
    assert(planFactory(true, 6, globalMAC) == FactoryPlan::Create);
    assert(planFactory(false, 6, mac) == FactoryPlan::Disabled);
    assert(planFactory(true, 6, nullptr) == FactoryPlan::Invalid);
    assert(planFactory(true, 6, zero) == FactoryPlan::Invalid);
    assert(planFactory(true, 6, multicast) == FactoryPlan::Invalid);
    assert(planFactory(true, 6, broadcast) == FactoryPlan::Invalid);
    for (uint32_t role = 0; role <= 10; ++role)
        if (role != 6)
            assert(planFactory(true, role, mac) == FactoryPlan::UnsupportedRole);
    puts("AWDL factory policy tests passed");
}
