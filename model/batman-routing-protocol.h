#ifndef BATMAN_ROUTING_PROTOCOL_H
#define BATMAN_ROUTING_PROTOCOL_H

#include "batman-header.h"

#include "ns3/event-id.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"

#include <map>
#include <utility>
#include <vector>

namespace ns3
{
namespace batman
{
class BatmanProtocolTestAccess;

/**
 * @brief Experimental BATMAN IPv4 routing protocol based on draft 00.
 *
 * Each IPv4 interface address is an independent originator. Route ranking is
 * based on per-neighbor counts in 16-bit sequence-number windows.
 */
class RoutingProtocol : public Ipv4RoutingProtocol
{
  public:
    /** @copydoc Ipv4RoutingProtocol::GetTypeId() */
    static TypeId GetTypeId();
    RoutingProtocol();
    ~RoutingProtocol() override;

    /** @copydoc Ipv4RoutingProtocol::RouteOutput() */
    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> packet,
                               const Ipv4Header& header,
                               Ptr<NetDevice> outputDevice,
                               Socket::SocketErrno& sockerr) override;
    /** @copydoc Ipv4RoutingProtocol::RouteInput() */
    bool RouteInput(Ptr<const Packet> packet,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> inputDevice,
                    const UnicastForwardCallback& unicastForward,
                    const MulticastForwardCallback& multicastForward,
                    const LocalDeliverCallback& localDeliver,
                    const ErrorCallback& error) override;
    /** @copydoc Ipv4RoutingProtocol::NotifyInterfaceUp() */
    void NotifyInterfaceUp(uint32_t interface) override;
    /** @copydoc Ipv4RoutingProtocol::NotifyInterfaceDown() */
    void NotifyInterfaceDown(uint32_t interface) override;
    /** @copydoc Ipv4RoutingProtocol::NotifyAddAddress() */
    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    /** @copydoc Ipv4RoutingProtocol::NotifyRemoveAddress() */
    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    /** @copydoc Ipv4RoutingProtocol::SetIpv4() */
    void SetIpv4(Ptr<Ipv4> ipv4) override;
    /** @copydoc Ipv4RoutingProtocol::PrintRoutingTable() */
    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override;

  private:
        friend class BatmanProtocolTestAccess;

    using NeighborKey = std::pair<uint32_t, Ipv4Address>;

    /** Per-originator receive state for one neighbor. */
    struct NeighborWindow
    {
        std::vector<uint8_t> received; ///< Seen sequence numbers, newest first.
        Time lastValid;                ///< Most recent in-window packet time.
        uint8_t lastTtl;               ///< TTL of the last globally new packet.
        bool hasLastTtl;               ///< Whether lastTtl has been initialized.
    };

    /** Sequence-window ranking state for one advertised originator. */
    struct OriginatorState
    {
        uint16_t newestSequence;                          ///< Current window upper bound.
        bool initialized;                                 ///< Whether a sequence has been accepted.
        std::vector<uint8_t> received;                    ///< Sequences seen from any neighbor.
        std::vector<uint8_t> forwarded; ///< Sequences already scheduled for relay.
        Time lastAware;                                   ///< Last time an OGM was received.
        std::map<NeighborKey, NeighborWindow> neighbors;  ///< Per-neighbor sequence windows.
        NeighborKey bestNeighbor;                        ///< Selected bidirectional next hop.
        bool hasBestNeighbor;                             ///< Whether a next hop is available.
    };

    /** Link state for a one-hop neighbor on one local interface. */
    struct NeighborState
    {
        std::vector<uint8_t> echoed; ///< Our OGM sequence numbers echoed by this neighbor.
        Time lastHeard;              ///< Most recent valid OGM from this neighbor.
    };

    /** Local originator identity and sequence state for one interface. */
    struct LocalOriginator
    {
        Ipv4Address address; ///< Address advertised for this interface.
        uint16_t sequence;   ///< Most recently originated sequence number.
        bool initialized;    ///< Whether an OGM has been originated.
    };

    /** Result of classifying one received sequence copy. */
    enum class Reception
    {
        NEW_SEQUENCE,
        DUPLICATE_OTHER_NEIGHBOR,
        DUPLICATE_SAME_NEIGHBOR,
        OUT_OF_WINDOW,
        UNIDIRECTIONAL
    };

    /** Create or update the socket and originator for an IPv4 interface.
     *
     * @param interface IPv4 interface index.
     */
    void EnsureSocket(uint32_t interface);
    /** Remove interface sockets, neighbor state and dependent routes.
     *
     * @param interface IPv4 interface index.
     */
    void CloseInterface(uint32_t interface);
    /** Originate one OGM per active IPv4 interface. */
    void SendOgm();
    /** Receive, classify, rank and possibly relay an OGM.
     *
     * @param socket Socket on which the OGM arrived.
     */
    void RecvOgm(Ptr<Socket> socket);
    /** Send a delayed relay if the selected route still permits it.
     *
     * @param header OGM to relay.
     * @param neighbor Neighbor from which the OGM was selected.
     * @param inputInterface Interface on which it arrived.
     * @param direct True when the OGM originator is the sending neighbor.
     * @param unidirectional True when that incoming link is not bidirectional.
     */
    void ForwardOgm(BatmanHeader header,
                    NeighborKey neighbor,
                    uint32_t inputInterface,
                    bool direct,
                    bool unidirectional);
    /** Purge originators and neighbors after the draft-derived timeout. */
    void Expire();
    /** Compare 16-bit sequence numbers using serial arithmetic.
     *
     * @param candidate Candidate sequence number.
     * @param current Current upper-bound sequence number.
     * @return True if candidate is newer within the unambiguous half-range.
     */
    bool IsNewer(uint16_t candidate, uint16_t current) const;
    /** Mark a sequence for forwarding once.
     *
     * @param originator Sequence-window state.
     * @param age Index of the sequence in the window.
     * @return True if this sequence had not already been scheduled.
     */
    bool MarkForwarded(OriginatorState& originator, uint32_t age) const;
    /** Advance a bitmap window by a sequence-number distance.
     *
     * @param window Window bitmap to move.
     * @param distance Number of sequence positions advanced.
     */
    void ShiftWindow(std::vector<uint8_t>& window, uint32_t distance) const;
    /** Count marked entries in a sequence window.
     *
     * @param window Window bitmap to count.
     * @return Number of received sequences.
     */
    uint32_t CountWindow(const std::vector<uint8_t>& window) const;
    /** Determine whether a neighbor has echoed a recent local OGM.
     *
     * @param neighbor Neighbor key.
     * @return True if its echo window is non-empty.
     */
    bool IsBidirectional(const NeighborKey& neighbor) const;
    /** Re-select and refresh an originator's route from window counts.
     *
     * @param originator Originator whose route is recalculated.
     */
    void RecomputeRoute(Ipv4Address originator);
    /** Construct a route through a selected neighbor.
     *
     * @param destination Route destination.
     * @param neighbor Selected next-hop neighbor.
     * @return Route, or nullptr if its interface is unavailable.
     */
    Ptr<Ipv4Route> MakeRoute(Ipv4Address destination, const NeighborKey& neighbor) const;
    /** Write a reception classification to the protocol debug log.
     *
     * @param reception Classification result.
     * @param originator OGM originator.
     * @param neighbor Receiving neighbor.
     */
    void LogReception(Reception reception,
                      Ipv4Address originator,
                      const NeighborKey& neighbor) const;

    Ptr<Ipv4> m_ipv4;                                      ///< Associated IPv4 stack.
    std::map<uint32_t, Ptr<Socket>> m_sockets;             ///< OGM sockets by interface.
    std::map<uint32_t, LocalOriginator> m_localOriginators; ///< Local originator per interface.
    std::map<NeighborKey, NeighborState> m_neighbors;      ///< One-hop link statistics.
    std::map<Ipv4Address, OriginatorState> m_originators;  ///< Learned originator windows/routes.
    std::vector<EventId> m_forwardEvents;                  ///< Pending jittered relay events.
    EventId m_ogmEvent;                                    ///< Next originator broadcast event.
    EventId m_expireEvent;                                 ///< Next state purge event.
    Time m_ogmInterval;                                    ///< OGM interval.
    Time m_routeTimeout; ///< Optional additional route-retention floor.
    uint32_t m_windowSize;                                 ///< Sequence-number window size.
    uint32_t m_port;                                       ///< OGM UDP port.
    uint8_t m_ttl;                                         ///< Initial OGM TTL.
    bool m_running;                                        ///< Whether periodic events are active.
    Ptr<UniformRandomVariable> m_jitter;                   ///< Random jitter source.
};
} // namespace batman
} // namespace ns3

#endif // BATMAN_ROUTING_PROTOCOL_H
