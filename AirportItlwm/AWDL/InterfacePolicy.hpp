/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef AIRPORT_AWDL_INTERFACE_POLICY_HPP
#define AIRPORT_AWDL_INTERFACE_POLICY_HPP
#include <stdint.h>
namespace AirportAWDL {
enum class FactoryPlan { Disabled, Invalid, UnsupportedRole, Create };
inline FactoryPlan planFactory(bool enabled, uint32_t role, const uint8_t *address)
{
    if (!enabled) return FactoryPlan::Disabled;
    // Tahoe IO80211VirtualInterface::createPeerManager selects AWDL at role 6.
    if (role != 6) return FactoryPlan::UnsupportedRole;
    if (!address || (address[0] & 1)) return FactoryPlan::Invalid;
    uint8_t bits = 0;
    for (unsigned i = 0; i < 6; ++i) bits |= address[i];
    return bits ? FactoryPlan::Create : FactoryPlan::Invalid;
}
}
#endif
