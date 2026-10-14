#ifndef BATMAN_ROUTING_PROTOCOL_H
#define BATMAN_ROUTING_PROTOCOL_H
#include "batman-header.h"

#include "ns3/event-id.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"

#include <map>
#include <set>
#include <utility>

namespace ns3
{
namespace batman
{
/**
 * @brief Experimental BATMAN-inspired proactive IPv4 routing protocol.
 *
 * This protocol is a simplified IPv4 routing prototype and is not compatible
 * with Linux batman-adv.
 */
class RoutingProtocol : public Ipv4RoutingProtocol
{
  public:
    /** @copydoc Ipv4RoutingProtocol::GetTypeId() */
    static TypeId GetTypeId();
    RoutingProtocol();
    ~RoutingProtocol() override;

    /** @copydoc Ipv4RoutingProtocol::RouteOutput() */
    Ptr<Ipv4Route> RouteOutput(Ptr<Packet>,
                               const Ipv4Header&,
                               Ptr<NetDevice>,
                               Socket::SocketErrno&) override;

    /** @copydoc Ipv4RoutingProtocol::RouteInput() */
    bool RouteInput(Ptr<const Packet>,
                    const Ipv4Header&,
                    Ptr<const NetDevice>,
                    const UnicastForwardCallback&,
                    const MulticastForwardCallback&,
                    const LocalDeliverCallback&,
                    const ErrorCallback&) override;

    /** @copydoc Ipv4RoutingProtocol::NotifyInterfaceUp() */
    void NotifyInterfaceUp(uint32_t) override;
    /** @copydoc Ipv4RoutingProtocol::NotifyInterfaceDown() */
    void NotifyInterfaceDown(uint32_t) override;
    /** @copydoc Ipv4RoutingProtocol::NotifyAddAddress() */
    void NotifyAddAddress(uint32_t, Ipv4InterfaceAddress) override;
    /** @copydoc Ipv4RoutingProtocol::NotifyRemoveAddress() */
    void NotifyRemoveAddress(uint32_t, Ipv4InterfaceAddress) override;
    /** @copydoc Ipv4RoutingProtocol::SetIpv4() */
    void SetIpv4(Ptr<Ipv4>) override;
    /** @copydoc Ipv4RoutingProtocol::PrintRoutingTable() */
    void PrintRoutingTable(Ptr<OutputStreamWrapper>, Time::Unit unit = Time::S) const override;

  private:
    /** Route information learned from an originator advertisement. */
    struct RouteEntry
    {
        Ipv4Address nextHop; ///< Neighbor used to reach the originator.
        uint32_t interface;  ///< Output interface index.
        uint8_t tq;          ///< Simplified route-quality value.
        uint32_t sequence;   ///< Newest accepted originator sequence number.
        Time updated;        ///< Time when this route was last refreshed.
    };

    /** Control-message socket and addressing information for one interface. */
    struct Iface
    {
        Ptr<Socket> socket;    ///< UDP socket used for control messages.
        uint32_t index;        ///< IPv4 interface index.
        Ipv4Address local;     ///< Local IPv4 address.
        Ipv4Address broadcast; ///< Interface broadcast address.
    };

    /** Create and bind a control socket when the interface is usable.
     *
     * @param interface IPv4 interface index.
     */
    void EnsureSocket(uint32_t interface);
    /** Close the control socket and remove routes using an interface.
     *
     * @param interface IPv4 interface index.
     */
    void CloseInterface(uint32_t interface);
    /** Advertise this node's originator message and schedule the next update. */
    void SendOgm();
    /** Process received advertisements and forward new messages.
     *
     * @param socket Socket on which the message arrived.
     */
    void RecvOgm(Ptr<Socket> socket);
    /** Remove expired routes and bound duplicate-cache growth. */
    void Expire();
    /** Send a control message on one interface.
     *
     * @param iface Interface state used for transmission.
     * @param header Control message to send.
     */
    void SendOn(const Iface& iface, const BatmanHeader& header);
    /** Compare wrapping 32-bit sequence numbers.
     *
     * @param lhs Candidate sequence number.
     * @param rhs Existing sequence number.
     * @return True if lhs is newer than rhs.
     */
    bool Newer(uint32_t lhs, uint32_t rhs) const;
    /** Construct an IPv4 route through a selected next hop.
     *
     * @param destination Route destination.
     * @param nextHop Gateway address.
     * @param interface Output interface index.
     * @return Constructed route, or nullptr if the interface is unavailable.
     */
    Ptr<Ipv4Route> MakeRoute(Ipv4Address destination,
                             Ipv4Address nextHop,
                             uint32_t interface) const;

    Ptr<Ipv4> m_ipv4;                                  ///< IPv4 stack using this routing protocol.
    std::map<uint32_t, Iface> m_sockets;               ///< Control sockets indexed by interface.
    std::map<Ipv4Address, RouteEntry> m_routes;        ///< Routes indexed by originator.
    std::set<std::pair<Ipv4Address, uint32_t>> m_seen; ///< Recently seen originator sequences.
    EventId m_ogmEvent;                  ///< Event that sends the next originator message.
    EventId m_expireEvent;               ///< Event that removes expired routing state.
    Time m_ogmInterval;                  ///< Nominal originator-message interval.
    Time m_routeTimeout;                 ///< Maximum route age.
    uint32_t m_port;                     ///< UDP port used for originator messages.
    uint32_t m_sequence;                 ///< Local originator sequence number.
    uint8_t m_ttl;                       ///< Initial originator-message lifetime.
    bool m_running;                      ///< Whether periodic protocol events are active.
    Ptr<UniformRandomVariable> m_jitter; ///< Random source for advertisement timing.
};
} // namespace batman
} // namespace ns3
#endif
