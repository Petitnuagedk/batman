#include "batman-helper.h"

#include "../model/batman-routing-protocol.h"

namespace ns3
{
BatmanHelper::BatmanHelper() = default;

BatmanHelper*
BatmanHelper::Copy() const
{
    return new BatmanHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
BatmanHelper::Create(Ptr<Node>) const
{
    return CreateObject<batman::RoutingProtocol>();
}
} // namespace ns3
