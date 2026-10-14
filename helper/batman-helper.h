#ifndef BATMAN_HELPER_H
#define BATMAN_HELPER_H
#include "ns3/ipv4-routing-helper.h"
namespace ns3 {
class BatmanHelper : public Ipv4RoutingHelper
{
  public:
    BatmanHelper();
    BatmanHelper* Copy() const override;
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;
};
}
#endif
