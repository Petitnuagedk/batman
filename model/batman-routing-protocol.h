#ifndef BATMAN_ROUTING_PROTOCOL_H
#define BATMAN_ROUTING_PROTOCOL_H
#include "batman-header.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/socket.h"
#include "ns3/event-id.h"
#include "ns3/random-variable-stream.h"
#include <map>
#include <set>
#include <utility>
namespace ns3 { namespace batman {
/** Experimental BATMAN-inspired proactive IPv4 routing protocol. Not batman-adv compatible. */
class RoutingProtocol : public Ipv4RoutingProtocol {
 public:
  static TypeId GetTypeId(); RoutingProtocol(); ~RoutingProtocol() override;
  Ptr<Ipv4Route> RouteOutput(Ptr<Packet>, const Ipv4Header&, Ptr<NetDevice>, Socket::SocketErrno&) override;
  bool RouteInput(Ptr<const Packet>, const Ipv4Header&, Ptr<const NetDevice>, const UnicastForwardCallback&, const MulticastForwardCallback&, const LocalDeliverCallback&, const ErrorCallback&) override;
  void NotifyInterfaceUp(uint32_t) override; void NotifyInterfaceDown(uint32_t) override;
  void NotifyAddAddress(uint32_t, Ipv4InterfaceAddress) override; void NotifyRemoveAddress(uint32_t, Ipv4InterfaceAddress) override;
  void SetIpv4(Ptr<Ipv4>) override; void PrintRoutingTable(Ptr<OutputStreamWrapper>, Time::Unit unit = Time::S) const override;
 private:
  struct RouteEntry { Ipv4Address nextHop; uint32_t interface; uint8_t tq; uint32_t sequence; Time updated; };
  struct Iface { Ptr<Socket> socket; uint32_t index; Ipv4Address local, broadcast; };
  void EnsureSocket(uint32_t); void CloseInterface(uint32_t); void SendOgm(); void RecvOgm(Ptr<Socket>); void Expire();
  void SendOn(const Iface&, const BatmanHeader&); bool Newer(uint32_t, uint32_t) const;
  Ptr<Ipv4Route> MakeRoute(Ipv4Address, Ipv4Address, uint32_t) const;
  Ptr<Ipv4> m_ipv4; std::map<uint32_t,Iface> m_sockets; std::map<Ipv4Address,RouteEntry> m_routes;
  std::set<std::pair<Ipv4Address,uint32_t>> m_seen; EventId m_ogmEvent,m_expireEvent;
  Time m_ogmInterval,m_routeTimeout; uint32_t m_port,m_sequence; uint8_t m_ttl; bool m_running;
  Ptr<UniformRandomVariable> m_jitter;
};
}}
#endif
