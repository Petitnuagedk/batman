#include "batman-routing-protocol.h"

#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <algorithm>

namespace ns3
{
namespace batman
{
NS_LOG_COMPONENT_DEFINE("BatmanRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

namespace
{
// Draft 00 section 8 specifies these upper bounds; uniform sampling is an
// implementation assumption because the draft permits jitter but does not
// prescribe its distribution.
constexpr uint32_t DRAFT_PURGE_WINDOW_MULTIPLIER = 10;
} // namespace

TypeId
RoutingProtocol::GetTypeId()
{
    static TypeId tid = TypeId("ns3::batman::RoutingProtocol")
                            .SetParent<Ipv4RoutingProtocol>()
                            .SetGroupName("Internet")
                            .AddConstructor<RoutingProtocol>()
                            .AddAttribute("OgmInterval",
                                          "Interval between originator messages.",
                                          TimeValue(Seconds(1)),
                                          MakeTimeAccessor(&RoutingProtocol::m_ogmInterval),
                                          MakeTimeChecker(MilliSeconds(10)))
                            .AddAttribute("RouteTimeout",
                                          "Additional route-retention floor; zero uses the "
                                          "draft-derived timeout.",
                                          TimeValue(Seconds(0)),
                                          MakeTimeAccessor(&RoutingProtocol::m_routeTimeout),
                                          MakeTimeChecker(Seconds(0)))
                            .AddAttribute("WindowSize",
                                          "Number of 16-bit sequence numbers in each "
                                          "ranking window.",
                                          UintegerValue(128),
                                          MakeUintegerAccessor(&RoutingProtocol::m_windowSize),
                                          MakeUintegerChecker<uint32_t>(1, 32767))
                            .AddAttribute("Port",
                                          "UDP port for BATMAN OGMs.",
                                          UintegerValue(4305),
                                          MakeUintegerAccessor(&RoutingProtocol::m_port),
                                          MakeUintegerChecker<uint32_t>(1, 65535))
                            .AddAttribute("Ttl",
                                          "Initial OGM time-to-live.",
                                          UintegerValue(50),
                                          MakeUintegerAccessor(&RoutingProtocol::m_ttl),
                                          MakeUintegerChecker<uint8_t>(2, 255));
    return tid;
}

RoutingProtocol::RoutingProtocol()
    : m_ogmInterval(Seconds(1)),
      m_routeTimeout(Seconds(0)),
      m_windowSize(128),
      m_port(4305),
      // Draft 00 permits a TTL in [2, 255] but does not specify a default.
      // Retain the prototype's in-range default rather than invent one.
      m_ttl(50),
      m_running(false),
      m_jitter(CreateObject<UniformRandomVariable>())
{
}

RoutingProtocol::~RoutingProtocol()
{
    m_ogmEvent.Cancel();
    m_expireEvent.Cancel();
    for (auto& event : m_forwardEvents)
    {
        event.Cancel();
    }
    for (auto& entry : m_sockets)
    {
        entry.second->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        entry.second->Close();
    }
}

void
RoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
    NS_ASSERT(ipv4);
    NS_ASSERT(!m_ipv4);
    m_ipv4 = ipv4;
    for (uint32_t interface = 1; interface < m_ipv4->GetNInterfaces(); ++interface)
    {
        if (m_ipv4->IsUp(interface))
        {
            EnsureSocket(interface);
        }
    }
    if (!m_running)
    {
        m_running = true;
        m_ogmEvent = Simulator::Schedule(MilliSeconds(m_jitter->GetInteger(0, 200)),
                                         &RoutingProtocol::SendOgm,
                                         this);
        m_expireEvent = Simulator::Schedule(m_ogmInterval, &RoutingProtocol::Expire, this);
    }
}

void
RoutingProtocol::EnsureSocket(uint32_t interface)
{
    if (!m_ipv4 || interface == 0 || interface >= m_ipv4->GetNInterfaces() ||
        !m_ipv4->IsUp(interface) || m_sockets.count(interface))
    {
        return;
    }
    for (uint32_t addressIndex = 0; addressIndex < m_ipv4->GetNAddresses(interface); ++addressIndex)
    {
        const Ipv4InterfaceAddress address = m_ipv4->GetAddress(interface, addressIndex);
        if (address.GetLocal() == Ipv4Address::GetLoopback() ||
            address.GetLocal() == Ipv4Address::GetZero())
        {
            continue;
        }

        Ptr<Socket> socket =
            Socket::CreateSocket(m_ipv4->GetObject<Node>(), UdpSocketFactory::GetTypeId());
        socket->SetAllowBroadcast(true);
        socket->BindToNetDevice(m_ipv4->GetNetDevice(interface));
        if (socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), m_port)) != 0)
        {
            socket->Close();
            continue;
        }
        socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvOgm, this));
        m_sockets[interface] = socket;
        m_localOriginators[interface] = {address.GetLocal(), 0, false};
        for (auto& neighbor : m_neighbors)
        {
            if (neighbor.first.first == interface)
            {
                neighbor.second.echoed.assign(m_windowSize, 0);
            }
        }
        return;
    }
}

void
RoutingProtocol::CloseInterface(uint32_t interface)
{
    auto socket = m_sockets.find(interface);
    if (socket != m_sockets.end())
    {
        socket->second->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        socket->second->Close();
        m_sockets.erase(socket);
    }
    m_localOriginators.erase(interface);

    for (auto neighbor = m_neighbors.begin(); neighbor != m_neighbors.end();)
    {
        if (neighbor->first.first == interface)
        {
            neighbor = m_neighbors.erase(neighbor);
        }
        else
        {
            ++neighbor;
        }
    }

    for (auto& originator : m_originators)
    {
        for (auto neighbor = originator.second.neighbors.begin();
             neighbor != originator.second.neighbors.end();)
        {
            if (neighbor->first.first == interface)
            {
                neighbor = originator.second.neighbors.erase(neighbor);
            }
            else
            {
                ++neighbor;
            }
        }
        RecomputeRoute(originator.first);
    }
}

void
RoutingProtocol::NotifyInterfaceUp(uint32_t interface)
{
    EnsureSocket(interface);
}

void
RoutingProtocol::NotifyInterfaceDown(uint32_t interface)
{
    CloseInterface(interface);
}

void
RoutingProtocol::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress)
{
    EnsureSocket(interface);
}

void
RoutingProtocol::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress)
{
    CloseInterface(interface);
    EnsureSocket(interface);
}

bool
RoutingProtocol::IsNewer(uint16_t candidate, uint16_t current) const
{
    const uint16_t distance = static_cast<uint16_t>(candidate - current);
    return distance != 0 && distance < 0x8000;
}

bool
RoutingProtocol::MarkForwarded(OriginatorState& originator, uint32_t age) const
{
    if (age >= originator.forwarded.size() || originator.forwarded[age])
    {
        return false;
    }
    originator.forwarded[age] = 1;
    return true;
}

void
RoutingProtocol::ShiftWindow(std::vector<uint8_t>& window, uint32_t distance) const
{
    if (distance >= window.size())
    {
        std::fill(window.begin(), window.end(), 0);
        return;
    }
    for (uint32_t index = window.size(); index-- > distance;)
    {
        window[index] = window[index - distance];
    }
    std::fill(window.begin(), window.begin() + distance, 0);
}

uint32_t
RoutingProtocol::CountWindow(const std::vector<uint8_t>& window) const
{
    return static_cast<uint32_t>(std::count(window.begin(), window.end(), 1));
}

bool
RoutingProtocol::IsBidirectional(const NeighborKey& neighbor) const
{
    const auto state = m_neighbors.find(neighbor);
    // Draft 00 omits BI_LINK_TIMEOUT; expire echo evidence after WINDOW_SIZE
    // local originator sequence advances as an explicit approximation.
    return state != m_neighbors.end() && CountWindow(state->second.echoed) != 0;
}

void
RoutingProtocol::SendOgm()
{
    if (!m_running)
    {
        return;
    }

    for (const auto& entry : m_sockets)
    {
        auto local = m_localOriginators.find(entry.first);
        if (local == m_localOriginators.end())
        {
            continue;
        }
        LocalOriginator& originator = local->second;
        originator.sequence = static_cast<uint16_t>(originator.sequence + 1);
        originator.initialized = true;

        BatmanHeader header;
        header.SetOriginator(originator.address);
        header.SetSequence(originator.sequence);
        header.SetTtl(m_ttl);
        header.SetGatewayFlags(0);
        header.SetGatewayPort(0);
        header.SetUnidirectional(false);
        header.SetDirectLink(false);

        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(header);
        if (entry.second->SendTo(packet,
                                 0,
                                 InetSocketAddress(Ipv4Address::GetBroadcast(), m_port)) < 0)
        {
            NS_LOG_DEBUG("OGM send failed on interface " << entry.first);
        }

        for (auto& neighbor : m_neighbors)
        {
            if (neighbor.first.first == entry.first)
            {
                ShiftWindow(neighbor.second.echoed, 1);
            }
        }
    }

    for (const auto& originator : m_originators)
    {
        RecomputeRoute(originator.first);
    }

    // Draft 00 permits up to 200 ms of originator jitter. Uniform sampling is
    // an explicit implementation assumption; the draft gives no distribution.
    const Time jitter = MilliSeconds(m_jitter->GetInteger(0, 200));
    m_ogmEvent = Simulator::Schedule(m_ogmInterval + jitter, &RoutingProtocol::SendOgm, this);
}

void
RoutingProtocol::LogReception(Reception reception,
                              Ipv4Address originator,
                              const NeighborKey& neighbor) const
{
    static const char* labels[] = {"new",
                                   "duplicate-via-other-neighbor",
                                   "duplicate-via-same-neighbor",
                                   "out-of-window",
                                   "unidirectional"};
    NS_LOG_DEBUG("OGM " << labels[static_cast<size_t>(reception)] << " originator=" << originator
                        << " sender=" << neighbor.second << " interface=" << neighbor.first);
}

void
RoutingProtocol::RecvOgm(Ptr<Socket> socket)
{
    Address from;
    Ptr<Packet> packet;
    while ((packet = socket->RecvFrom(from)))
    {
        if (!InetSocketAddress::IsMatchingType(from) ||
            packet->GetSize() < BatmanHeader().GetSerializedSize())
        {
            continue;
        }

        const InetSocketAddress sender = InetSocketAddress::ConvertFrom(from);
        const auto interfaceSocket = std::find_if(
            m_sockets.begin(),
            m_sockets.end(),
            [socket](const auto& entry) { return entry.second == socket; });
        if (interfaceSocket == m_sockets.end())
        {
            continue;
        }
        const uint32_t interface = interfaceSocket->first;
        const NeighborKey neighbor(interface, sender.GetIpv4());
        const bool senderIsLocal = std::any_of(
            m_localOriginators.begin(),
            m_localOriginators.end(),
            [&sender](const auto& entry) { return entry.second.address == sender.GetIpv4(); });
        if (senderIsLocal)
        {
            continue;
        }

        BatmanHeader header;
        packet->RemoveHeader(header);
        if (header.GetVersion() != BatmanHeader::VERSION || header.GetTtl() == 0 ||
            sender.GetIpv4() == Ipv4Address::GetZero())
        {
            continue;
        }
        const auto localAddress = std::find_if(
            m_localOriginators.begin(),
            m_localOriginators.end(),
            [&header](const auto& entry) {
                return entry.second.address == header.GetOriginator();
            });

        // A self-originated OGM is accepted only as a direct-link echo on the
        // same interface and only while its sequence remains in our own window.
        if (localAddress != m_localOriginators.end())
        {
            if (localAddress->first != interface || !header.IsDirectLink() ||
                !localAddress->second.initialized)
            {
                continue;
            }
            const uint16_t age = static_cast<uint16_t>(localAddress->second.sequence -
                                                       header.GetSequence());
            if (age >= m_windowSize)
            {
                continue;
            }
            NeighborState& state = m_neighbors[neighbor];
            if (state.echoed.size() != m_windowSize)
            {
                state.echoed.assign(m_windowSize, 0);
            }
            state.echoed[age] = 1;
            state.lastHeard = Simulator::Now();
            for (const auto& originator : m_originators)
            {
                RecomputeRoute(originator.first);
            }
            continue;
        }

        if (sender.GetIpv4() == header.GetOriginator())
        {
            NeighborState& state = m_neighbors[neighbor];
            if (state.echoed.size() != m_windowSize)
            {
                state.echoed.assign(m_windowSize, 0);
            }
            state.lastHeard = Simulator::Now();
        }

        if (header.IsUnidirectional())
        {
            LogReception(Reception::UNIDIRECTIONAL, header.GetOriginator(), neighbor);
            continue;
        }

        auto [originatorIt, inserted] = m_originators.try_emplace(header.GetOriginator());
        OriginatorState& originator = originatorIt->second;
        if (inserted)
        {
            originator.initialized = false;
            originator.hasBestNeighbor = false;
        }
        if (originator.received.size() != m_windowSize)
        {
            originator.received.assign(m_windowSize, 0);
            originator.forwarded.assign(m_windowSize, 0);
        }

        bool sequenceAdvanced = false;
        if (!originator.initialized)
        {
            originator.newestSequence = header.GetSequence();
            originator.initialized = true;
            sequenceAdvanced = true;
        }
        else if (IsNewer(header.GetSequence(), originator.newestSequence))
        {
            const uint16_t distance = static_cast<uint16_t>(header.GetSequence() -
                                                            originator.newestSequence);
            ShiftWindow(originator.received, distance);
            ShiftWindow(originator.forwarded, distance);
            for (auto& link : originator.neighbors)
            {
                ShiftWindow(link.second.received, distance);
            }
            originator.newestSequence = header.GetSequence();
            sequenceAdvanced = true;
        }
        originator.lastAware = Simulator::Now();

        const uint16_t age = static_cast<uint16_t>(originator.newestSequence -
                                                   header.GetSequence());
        if (age >= m_windowSize)
        {
            LogReception(Reception::OUT_OF_WINDOW, header.GetOriginator(), neighbor);
            continue;
        }

        NeighborWindow& link = originator.neighbors[neighbor];
        if (link.received.size() != m_windowSize)
        {
            link.received.assign(m_windowSize, 0);
            link.hasLastTtl = false;
        }
        const bool globallySeen = originator.received[age] != 0;
        const bool seenViaNeighbor = link.received[age] != 0;
        Reception reception = Reception::NEW_SEQUENCE;
        if (seenViaNeighbor)
        {
            reception = Reception::DUPLICATE_SAME_NEIGHBOR;
        }
        else if (globallySeen)
        {
            reception = Reception::DUPLICATE_OTHER_NEIGHBOR;
        }

        originator.received[age] = 1;
        link.received[age] = 1;
        link.lastValid = Simulator::Now();
        if (sequenceAdvanced)
        {
            link.lastTtl = header.GetTtl();
            link.hasLastTtl = true;
        }

        const bool direct = sender.GetIpv4() == header.GetOriginator();
        NeighborState& neighborState = m_neighbors[neighbor];
        if (neighborState.echoed.size() != m_windowSize)
        {
            neighborState.echoed.assign(m_windowSize, 0);
        }
        neighborState.lastHeard = Simulator::Now();
        if (!IsBidirectional(neighbor))
        {
            reception = Reception::UNIDIRECTIONAL;
        }
        LogReception(reception, header.GetOriginator(), neighbor);
        RecomputeRoute(header.GetOriginator());

        const bool selectedBest = originator.hasBestNeighbor && originator.bestNeighbor == neighbor;
        const bool ttlMatchesLastNew = link.hasLastTtl && header.GetTtl() == link.lastTtl;
        const bool shouldRelay =
            direct || (IsBidirectional(neighbor) && selectedBest &&
                       (reception == Reception::NEW_SEQUENCE || ttlMatchesLastNew));
        if (shouldRelay && header.GetTtl() > 1 && MarkForwarded(originator, age))
        {
            const Time delay = MilliSeconds(m_jitter->GetInteger(0, 100));
            m_forwardEvents.push_back(Simulator::Schedule(delay,
                                                          &RoutingProtocol::ForwardOgm,
                                                          this,
                                                          header,
                                                          neighbor,
                                                          interface,
                                                          direct,
                                                          !IsBidirectional(neighbor)));
        }
    }
}

void
RoutingProtocol::ForwardOgm(BatmanHeader header,
                            NeighborKey neighbor,
                            uint32_t inputInterface,
                            bool direct,
                            bool unidirectional)
{
    if (!direct)
    {
        auto originator = m_originators.find(header.GetOriginator());
        if (originator == m_originators.end() || !originator->second.hasBestNeighbor ||
            originator->second.bestNeighbor != neighbor || !IsBidirectional(neighbor))
        {
            return;
        }
    }

    if (header.GetTtl() <= 1)
    {
        return;
    }
    header.SetTtl(header.GetTtl() - 1);
    header.SetUnidirectional(unidirectional);
    for (const auto& entry : m_sockets)
    {
        BatmanHeader outgoing = header;
        outgoing.SetDirectLink(direct && entry.first == inputInterface);
        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(outgoing);
        if (entry.second->SendTo(packet,
                                 0,
                                 InetSocketAddress(Ipv4Address::GetBroadcast(), m_port)) < 0)
        {
            NS_LOG_DEBUG("OGM relay failed on interface " << entry.first);
        }
    }
}

void
RoutingProtocol::Expire()
{
    const Time now = Simulator::Now();
    const Time purgeTimeout = std::max(m_routeTimeout,
                                       m_ogmInterval *
                                           (DRAFT_PURGE_WINDOW_MULTIPLIER * m_windowSize));

    for (auto originator = m_originators.begin(); originator != m_originators.end();)
    {
        if (now - originator->second.lastAware > purgeTimeout)
        {
            originator = m_originators.erase(originator);
            continue;
        }
        for (auto neighbor = originator->second.neighbors.begin();
             neighbor != originator->second.neighbors.end();)
        {
            if (now - neighbor->second.lastValid > purgeTimeout)
            {
                neighbor = originator->second.neighbors.erase(neighbor);
            }
            else
            {
                ++neighbor;
            }
        }
        RecomputeRoute(originator->first);
        ++originator;
    }

    for (auto neighbor = m_neighbors.begin(); neighbor != m_neighbors.end();)
    {
        if (now - neighbor->second.lastHeard > purgeTimeout)
        {
            neighbor = m_neighbors.erase(neighbor);
            for (const auto& originator : m_originators)
            {
                RecomputeRoute(originator.first);
            }
        }
        else
        {
            ++neighbor;
        }
    }

    m_forwardEvents.erase(std::remove_if(m_forwardEvents.begin(),
                                         m_forwardEvents.end(),
                                         [](const EventId& event) { return event.IsExpired(); }),
                          m_forwardEvents.end());
    m_expireEvent = Simulator::Schedule(m_ogmInterval, &RoutingProtocol::Expire, this);
}

void
RoutingProtocol::RecomputeRoute(Ipv4Address address)
{
    auto state = m_originators.find(address);
    if (state == m_originators.end())
    {
        return;
    }

    // Draft 00 ranks each neighbor by received-sequence count in the window;
    // it defines no TQ, asymmetry, or per-hop penalty formula.
    uint32_t bestCount = 0;
    NeighborKey candidate{};
    bool haveCandidate = false;
    const auto incumbent = state->second.neighbors.find(state->second.bestNeighbor);
    const bool incumbentValid = state->second.hasBestNeighbor &&
                                incumbent != state->second.neighbors.end() &&
                                IsBidirectional(state->second.bestNeighbor);
    const uint32_t incumbentCount = incumbentValid ? CountWindow(incumbent->second.received) : 0;
    for (const auto& link : state->second.neighbors)
    {
        if (!IsBidirectional(link.first))
        {
            continue;
        }
        const uint32_t count = CountWindow(link.second.received);
        if (count > bestCount)
        {
            bestCount = count;
            candidate = link.first;
            haveCandidate = count != 0;
        }
        else if (count != 0 && count == bestCount && haveCandidate && link.first < candidate)
        {
            candidate = link.first;
        }
    }

    if (incumbentValid && incumbentCount >= bestCount)
    {
        candidate = state->second.bestNeighbor;
        bestCount = incumbentCount;
        haveCandidate = bestCount != 0;
    }

    if (bestCount == 0 || !haveCandidate)
    {
        state->second.hasBestNeighbor = false;
        return;
    }

    state->second.bestNeighbor = candidate;
    state->second.hasBestNeighbor = true;
}

Ptr<Ipv4Route>
RoutingProtocol::MakeRoute(Ipv4Address destination, const NeighborKey& neighbor) const
{
    if (!m_ipv4 || neighbor.first >= m_ipv4->GetNInterfaces() ||
        !m_ipv4->IsUp(neighbor.first) || m_ipv4->GetNAddresses(neighbor.first) == 0)
    {
        return nullptr;
    }
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(destination);
    route->SetGateway(neighbor.second);
    route->SetOutputDevice(m_ipv4->GetNetDevice(neighbor.first));
    route->SetSource(m_ipv4->GetAddress(neighbor.first, 0).GetLocal());
    return route;
}

Ptr<Ipv4Route>
RoutingProtocol::RouteOutput(Ptr<Packet>,
                             const Ipv4Header& header,
                             Ptr<NetDevice> outputDevice,
                             Socket::SocketErrno& sockerr)
{
    auto originator = m_originators.find(header.GetDestination());
    if (originator == m_originators.end() || !originator->second.hasBestNeighbor)
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }
    const NeighborKey& nextHop = originator->second.bestNeighbor;
    if (outputDevice && outputDevice != m_ipv4->GetNetDevice(nextHop.first))
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }
    Ptr<Ipv4Route> route = MakeRoute(header.GetDestination(), nextHop);
    sockerr = route ? Socket::ERROR_NOTERROR : Socket::ERROR_NOROUTETOHOST;
    return route;
}

bool
RoutingProtocol::RouteInput(Ptr<const Packet> packet,
                            const Ipv4Header& header,
                            Ptr<const NetDevice> inputDevice,
                            const UnicastForwardCallback& unicastForward,
                            const MulticastForwardCallback&,
                            const LocalDeliverCallback& localDeliver,
                            const ErrorCallback& error)
{
    if (!m_ipv4)
    {
        return false;
    }
    const int32_t input = m_ipv4->GetInterfaceForDevice(inputDevice);
    if (input >= 0 &&
        m_ipv4->IsDestinationAddress(header.GetDestination(), static_cast<uint32_t>(input)))
    {
        localDeliver(packet, header, static_cast<uint32_t>(input));
        return true;
    }

    auto originator = m_originators.find(header.GetDestination());
    if (originator != m_originators.end() && originator->second.hasBestNeighbor)
    {
        const NeighborKey& nextHop = originator->second.bestNeighbor;
        if (nextHop.second == header.GetSource())
        {
            error(packet, header, Socket::ERROR_NOROUTETOHOST);
            return true;
        }
        Ptr<Ipv4Route> route = MakeRoute(header.GetDestination(), nextHop);
        if (route)
        {
            unicastForward(route, packet, header);
            return true;
        }
    }

    // Draft 00 requires host routes for bidirectional neighbors. A shared
    // subnet alone is not evidence that an arbitrary destination is reachable.
    error(packet, header, Socket::ERROR_NOROUTETOHOST);
    return true;
}

void
RoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    auto output = stream->GetStream();
    *output << "BATMAN-IV routing table at " << Simulator::Now().As(unit)
            << "\nDestination\tNextHop\tIf\tWindowCount\tAge\n";
    for (const auto& originator : m_originators)
    {
        if (!originator.second.hasBestNeighbor)
        {
            continue;
        }
        const NeighborKey& nextHop = originator.second.bestNeighbor;
        const auto window = originator.second.neighbors.find(nextHop);
        const uint32_t count = window == originator.second.neighbors.end()
                                  ? 0
                                  : CountWindow(window->second.received);
        *output << originator.first << '\t' << nextHop.second << '\t' << nextHop.first << '\t'
                << count << '\t' << (Simulator::Now() - originator.second.lastAware).As(unit)
                << '\n';
    }
}
} // namespace batman
} // namespace ns3
