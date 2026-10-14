#ifndef BATMAN_HELPER_H
#define BATMAN_HELPER_H
#include "ns3/ipv4-routing-helper.h"

namespace ns3
{
/**
 * @brief Helper that installs the BATMAN-inspired IPv4 routing protocol.
 */
class BatmanHelper : public Ipv4RoutingHelper
{
  public:
    BatmanHelper();
    /** @copydoc Ipv4RoutingHelper::Copy() */
    BatmanHelper* Copy() const override;
    /** @copydoc Ipv4RoutingHelper::Create() */
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;
};
} // namespace ns3
#endif
