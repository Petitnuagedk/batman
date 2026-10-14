#include "../helper/batman-helper.h"
#include "../model/batman-header.h"
#include "../model/batman-routing-protocol.h"

#include "ns3/applications-module.h"
#include "ns3/error-model.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/packet.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/test.h"
#include "ns3/wifi-module.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

namespace ns3
{
namespace batman
{
/** Test-only access to protocol internals for deterministic edge-case checks. */
class BatmanProtocolTestAccess
{
  public:
    /** Check the implementation's 16-bit serial-number comparison. */
    static bool IsNewer(const RoutingProtocol& protocol, uint16_t candidate, uint16_t current)
    {
        return protocol.IsNewer(candidate, current);
    }

    /** Check that a sequence can be scheduled for relay at most once. */
    static bool MarkForwardsOnce(RoutingProtocol& protocol)
    {
        RoutingProtocol::OriginatorState state{};
        state.forwarded.assign(2, 0);
        const bool first = protocol.MarkForwarded(state, 0);
        const bool replay = protocol.MarkForwarded(state, 0);
        const bool invalidIndex = protocol.MarkForwarded(state, 2);
        return first && !replay && !invalidIndex;
    }

    /** Seed an initial, single-sample route to exercise bad-first convergence. */
    static void SeedOneSample(Ptr<RoutingProtocol> protocol,
                              Ipv4Address originatorAddress,
                              Ipv4Address neighborAddress,
                              uint32_t interface)
    {
        const RoutingProtocol::NeighborKey key(interface, neighborAddress);
        RoutingProtocol::OriginatorState& originator = protocol->m_originators[originatorAddress];
        originator.newestSequence = 0;
        originator.initialized = true;
        originator.received.assign(protocol->m_windowSize, 0);
        originator.received[0] = 1;
        originator.forwarded.assign(protocol->m_windowSize, 0);
        originator.lastAware = Simulator::Now();
        originator.hasBestNeighbor = false;

        RoutingProtocol::NeighborWindow& window = originator.neighbors[key];
        window.received.assign(protocol->m_windowSize, 0);
        window.received[0] = 1;
        window.lastValid = Simulator::Now();
        window.hasLastTtl = false;

        RoutingProtocol::NeighborState& neighbor = protocol->m_neighbors[key];
        neighbor.echoed.assign(protocol->m_windowSize, 0);
        neighbor.echoed[0] = 1;
        neighbor.lastHeard = Simulator::Now();
        protocol->RecomputeRoute(originatorAddress);
    }

    /** Report whether an OGM from a direct neighbor was observed. */
    static bool HasHeard(Ptr<RoutingProtocol> protocol,
                         Ipv4Address neighborAddress,
                         uint32_t interface)
    {
        return protocol->m_neighbors.count({interface, neighborAddress}) != 0;
    }
};
} // namespace batman

namespace
{
struct WifiTestNetwork
{
    NodeContainer nodes;
    NetDeviceContainer devices;
    Ipv4InterfaceContainer addresses;
};

WifiTestNetwork
CreateWifiNetwork(const std::vector<Vector>& positions,
                  double range,
                  uint32_t lossyNode = UINT32_MAX,
                  double errorRate = 0.0)
{
    WifiTestNetwork network;
    network.nodes.Create(positions.size());

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    channel.AddPropagationLoss("ns3::RangePropagationLossModel",
                               "MaxRange",
                               DoubleValue(range));
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("ErpOfdmRate6Mbps"),
                                 "ControlMode",
                                 StringValue("ErpOfdmRate6Mbps"));
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    network.devices = wifi.Install(phy, mac, network.nodes);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> allocator = CreateObject<ListPositionAllocator>();
    for (const Vector& position : positions)
    {
        allocator->Add(position);
    }
    mobility.SetPositionAllocator(allocator);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(network.nodes);

    if (lossyNode < network.devices.GetN())
    {
        Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(network.devices.Get(lossyNode));
        Ptr<RateErrorModel> errors = CreateObject<RateErrorModel>();
        errors->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        errors->SetAttribute("ErrorRate", DoubleValue(errorRate));
        device->GetPhy()->SetPostReceptionErrorModel(errors);
    }

    BatmanHelper routing;
    InternetStackHelper internet;
    internet.SetRoutingHelper(routing);
    internet.Install(network.nodes);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.77.0.0", "255.255.255.0");
    network.addresses = addresses.Assign(network.devices);
    return network;
}

Ptr<batman::RoutingProtocol>
GetProtocol(Ptr<Node> node)
{
    return DynamicCast<batman::RoutingProtocol>(node->GetObject<Ipv4>()->GetRoutingProtocol());
}

Ptr<Ipv4Route>
LookupRoute(Ptr<Node> node, Ipv4Address destination)
{
    Ipv4Header header;
    header.SetDestination(destination);
    Socket::SocketErrno error = Socket::ERROR_NOTERROR;
    return GetProtocol(node)->RouteOutput(Create<Packet>(), header, nullptr, error);
}

struct FlowMetrics
{
    uint64_t txPackets{0};
    uint64_t rxPackets{0};
    double pdr{0.0};
    double meanDelaySeconds{0.0};
};

FlowMetrics
RunFlowTrial(uint32_t seed)
{
    RngSeedManager::SetSeed(seed);
    const double stopTime = 12.0;
    WifiTestNetwork network = CreateWifiNetwork(
        {{0, 0, 0}, {50, 0, 0}, {100, 0, 0}, {150, 0, 0}, {200, 0, 0}}, 55.0);

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();
    const uint16_t port = 9000;
    UdpServerHelper server(port);
    ApplicationContainer serverApps = server.Install(network.nodes.Get(4));
    serverApps.Start(Seconds(0.5));
    serverApps.Stop(Seconds(stopTime));
    UdpClientHelper client(network.addresses.GetAddress(4), port);
    client.SetAttribute("MaxPackets", UintegerValue(100000));
    client.SetAttribute("Interval", TimeValue(MilliSeconds(250)));
    client.SetAttribute("PacketSize", UintegerValue(512));
    ApplicationContainer clientApps = client.Install(network.nodes.Get(0));
    clientApps.Start(Seconds(5.0));
    clientApps.Stop(Seconds(stopTime - 0.1));

    Simulator::Stop(Seconds(stopTime));
    Simulator::Run();
    monitor->CheckForLostPackets();

    FlowMetrics result;
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    Time delaySum;
    for (const auto& entry : monitor->GetFlowStats())
    {
        const Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(entry.first);
        if (tuple.destinationPort == port)
        {
            result.txPackets += entry.second.txPackets;
            result.rxPackets += entry.second.rxPackets;
            delaySum += entry.second.delaySum;
        }
    }
    if (result.txPackets != 0)
    {
        result.pdr = static_cast<double>(result.rxPackets) / result.txPackets;
    }
    if (result.rxPackets != 0)
    {
        result.meanDelaySeconds = delaySum.GetSeconds() / result.rxPackets;
    }
    Simulator::Destroy();
    return result;
}

std::pair<double, double>
MeanAndSpread(const std::vector<double>& values)
{
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    double squaredDifferences = 0.0;
    for (double value : values)
    {
        squaredDifferences += (value - mean) * (value - mean);
    }
    return {mean, std::sqrt(squaredDifferences / values.size())};
}

class BatmanHeaderTestCase : public TestCase
{
  public:
    BatmanHeaderTestCase()
        : TestCase("BATMAN-IV OGM serializes exact draft fields and flags")
    {
    }

  private:
    void DoRun() override
    {
        BatmanHeader header;
        header.SetOriginator(Ipv4Address("10.1.2.3"));
        header.SetSequence(0x5678);
        header.SetTtl(42);
        header.SetGatewayFlags(0x35);
        header.SetGatewayPort(4305);
        header.SetUnidirectional(true);
        header.SetDirectLink(true);

        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(header);
        std::array<uint8_t, 12> bytes{};
        packet->CopyData(bytes.data(), bytes.size());
        NS_TEST_ASSERT_MSG_EQ(packet->GetSize(), bytes.size(), "OGM must be 12 octets");
        NS_TEST_ASSERT_MSG_EQ(bytes[0], BatmanHeader::VERSION, "version octet");
        NS_TEST_ASSERT_MSG_EQ(bytes[1], 0xc0, "U/D flag positions");
        NS_TEST_ASSERT_MSG_EQ(bytes[2], 42, "TTL octet");
        NS_TEST_ASSERT_MSG_EQ(bytes[3], 0x35, "gateway flags octet");
        NS_TEST_ASSERT_MSG_EQ(bytes[4], 0x56, "sequence high octet");
        NS_TEST_ASSERT_MSG_EQ(bytes[5], 0x78, "sequence low octet");
        NS_TEST_ASSERT_MSG_EQ(bytes[6], 0x10, "gateway port high octet");
        NS_TEST_ASSERT_MSG_EQ(bytes[7], 0xd1, "gateway port low octet");
        NS_TEST_ASSERT_MSG_EQ(bytes[8], 10, "originator octet 1");
        NS_TEST_ASSERT_MSG_EQ(bytes[11], 3, "originator octet 4");

        BatmanHeader decoded;
        packet->RemoveHeader(decoded);
        NS_TEST_ASSERT_MSG_EQ(decoded.GetOriginator(), Ipv4Address("10.1.2.3"),
                              "originator round trip");
        NS_TEST_ASSERT_MSG_EQ(decoded.GetVersion(), BatmanHeader::VERSION,
                              "version round trip");
        NS_TEST_ASSERT_MSG_EQ(decoded.GetSequence(), 0x5678, "sequence round trip");
        NS_TEST_ASSERT_MSG_EQ(decoded.GetGatewayPort(), 4305, "gateway port round trip");
        NS_TEST_ASSERT_MSG_EQ(decoded.IsUnidirectional(), true, "U flag round trip");
        NS_TEST_ASSERT_MSG_EQ(decoded.IsDirectLink(), true, "D flag round trip");
    }
};

class BatmanSequenceWrapTestCase : public TestCase
{
  public:
    BatmanSequenceWrapTestCase()
        : TestCase("BATMAN-IV sequence ordering wraps at 16 bits")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<batman::RoutingProtocol> protocol = CreateObject<batman::RoutingProtocol>();
        NS_TEST_ASSERT_MSG_EQ(batman::BatmanProtocolTestAccess::IsNewer(*protocol, 0, 65535),
                              true,
                              "zero follows 65535");
        NS_TEST_ASSERT_MSG_EQ(batman::BatmanProtocolTestAccess::IsNewer(*protocol, 65535, 0),
                              false,
                              "65535 is older than zero after wrap");
        NS_TEST_ASSERT_MSG_EQ(batman::BatmanProtocolTestAccess::IsNewer(*protocol, 100, 100),
                              false,
                              "equal sequence is not newer");
    }
};

class BatmanReplayTestCase : public TestCase
{
  public:
    BatmanReplayTestCase()
        : TestCase("BATMAN-IV duplicate and replay sequences are not reflooded")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<batman::RoutingProtocol> protocol = CreateObject<batman::RoutingProtocol>();
        NS_TEST_ASSERT_MSG_EQ(batman::BatmanProtocolTestAccess::MarkForwardsOnce(*protocol),
                              true,
                              "first in-window sequence is scheduled once; replay is rejected");
    }
};

class BatmanLossyPathTestCase : public TestCase
{
  public:
    BatmanLossyPathTestCase()
        : TestCase("BATMAN-IV selects the reliable path after a lossy first sample")
    {
    }

  private:
    void DoRun() override
    {
        RngSeedManager::SetSeed(17);
        WifiTestNetwork network = CreateWifiNetwork(
            {{0, 0, 0}, {40, 0, 0}, {0, 40, 0}, {40, 40, 0}}, 50.0, 2, 0.4);
        const Ipv4Address destination = network.addresses.GetAddress(3);
        Ptr<batman::RoutingProtocol> source = GetProtocol(network.nodes.Get(0));
        batman::BatmanProtocolTestAccess::SeedOneSample(
            source, destination, network.addresses.GetAddress(2), 1);

        Ipv4Address firstHop;
        Simulator::Schedule(Seconds(0.01), [&]() {
            Ptr<Ipv4Route> initial = LookupRoute(network.nodes.Get(0), destination);
            if (initial)
            {
                firstHop = initial->GetGateway();
            }
        });
        Simulator::Stop(Seconds(12));
        Simulator::Run();

        Ptr<Ipv4Route> selected = LookupRoute(network.nodes.Get(0), destination);
        NS_TEST_ASSERT_MSG_EQ(firstHop, network.addresses.GetAddress(2),
                              "the injected first observation is the lossy neighbor");
        NS_TEST_ASSERT_MSG_EQ(selected != nullptr, true, "destination has a selected route");
        if (selected)
        {
            NS_TEST_ASSERT_MSG_EQ(selected->GetGateway(), network.addresses.GetAddress(1),
                                  "the higher-count reliable neighbor wins");
        }
        Simulator::Destroy();
    }
};

class BatmanAsymmetricLinkTestCase : public TestCase
{
  public:
    BatmanAsymmetricLinkTestCase()
        : TestCase("BATMAN-IV does not install a route over an asymmetric link")
    {
    }

  private:
    void DoRun() override
    {
        WifiTestNetwork network = CreateWifiNetwork({{0, 0, 0}, {40, 0, 0}}, 50.0, 1, 1.0);
        Simulator::Stop(Seconds(6));
        Simulator::Run();

        Ptr<batman::RoutingProtocol> a = GetProtocol(network.nodes.Get(0));
        Ptr<batman::RoutingProtocol> b = GetProtocol(network.nodes.Get(1));
        NS_TEST_ASSERT_MSG_EQ(batman::BatmanProtocolTestAccess::HasHeard(
                                  a, network.addresses.GetAddress(1), 1),
                              true,
                              "A receives B's direct OGMs");
        NS_TEST_ASSERT_MSG_EQ(LookupRoute(network.nodes.Get(0), network.addresses.GetAddress(1)) !=
                                  nullptr,
                              false,
                              "A cannot route through a neighbor that cannot hear A");
        NS_TEST_ASSERT_MSG_EQ(LookupRoute(network.nodes.Get(1), network.addresses.GetAddress(0)) !=
                                  nullptr,
                              false,
                              "B has no route over the one-way link");
        Simulator::Destroy();
    }
};

class BatmanFailoverBoundTestCase : public TestCase
{
  public:
    BatmanFailoverBoundTestCase()
        : TestCase("BATMAN-IV fails over within the sequence-window bound")
    {
    }

  private:
    void DoRun() override
    {
        WifiTestNetwork network = CreateWifiNetwork(
            {{0, 0, 0}, {40, 0, 0}, {0, 40, 0}, {40, 40, 0}}, 50.0);
        const Ipv4Address destination = network.addresses.GetAddress(3);
        Simulator::Stop(Seconds(6));
        Simulator::Run();

        Ptr<Ipv4Route> initial = LookupRoute(network.nodes.Get(0), destination);
        NS_TEST_ASSERT_MSG_EQ(initial != nullptr, true, "diamond topology converges");
        if (!initial)
        {
            Simulator::Destroy();
            return;
        }
        uint32_t failedIndex = initial->GetGateway() == network.addresses.GetAddress(1) ? 1 : 2;
        const uint32_t survivingIndex = failedIndex == 1 ? 2 : 1;
        Ptr<ConstantPositionMobilityModel> failedMobility =
            network.nodes.Get(failedIndex)->GetObject<ConstantPositionMobilityModel>();
        failedMobility->SetPosition(Vector(500, 0, 0));
        const Ipv4Address expectedHop = network.addresses.GetAddress(survivingIndex);
        bool recovered = false;
        Time recoveryTime;
        const Time failureTime = Simulator::Now();
        const Time recoveryBound = Seconds(128 * 1.2 + 2.0);
        std::function<void()> poll;
        poll = [&]() {
            Ptr<Ipv4Route> route = LookupRoute(network.nodes.Get(0), destination);
            if (route && route->GetGateway() == expectedHop)
            {
                recovered = true;
                recoveryTime = Simulator::Now() - failureTime;
            }
            else if (Simulator::Now() < failureTime + recoveryBound)
            {
                Simulator::Schedule(Seconds(1), poll);
            }
        };
        Simulator::Schedule(Seconds(1), poll);
        Simulator::Stop(recoveryBound + Seconds(1));
        Simulator::Run();

        NS_TEST_ASSERT_MSG_EQ(recovered, true, "route uses the surviving path again");
        if (recovered)
        {
            NS_TEST_ASSERT_MSG_EQ(recoveryTime <= recoveryBound, true,
                                  "recovery bound is WINDOW_SIZE * (interval + max jitter) + 2s");
        }
        Simulator::Destroy();
    }
};

class BatmanMobilityTestCase : public TestCase
{
  public:
    BatmanMobilityTestCase()
        : TestCase("BATMAN-IV expires stale direct routes and recovers after return")
    {
    }

  private:
    void DoRun() override
    {
        WifiTestNetwork network = CreateWifiNetwork({{0, 0, 0}, {40, 0, 0}}, 50.0);
        const Ipv4Address destination = network.addresses.GetAddress(1);
        Simulator::Stop(Seconds(5));
        Simulator::Run();
        NS_TEST_ASSERT_MSG_EQ(LookupRoute(network.nodes.Get(0), destination) != nullptr,
                              true,
                              "direct bidirectional route is learned");

        Ptr<ConstantPositionMobilityModel> mobility =
            network.nodes.Get(1)->GetObject<ConstantPositionMobilityModel>();
        mobility->SetPosition(Vector(250, 0, 0));
        Simulator::Stop(Seconds(160));
        Simulator::Run();
        NS_TEST_ASSERT_MSG_EQ(LookupRoute(network.nodes.Get(0), destination) != nullptr,
                              false,
                              "echo window expiry removes the stale direct route");

        mobility->SetPosition(Vector(40, 0, 0));
        Simulator::Stop(Seconds(8));
        Simulator::Run();
        NS_TEST_ASSERT_MSG_EQ(LookupRoute(network.nodes.Get(0), destination) != nullptr,
                              true,
                              "direct route is relearned after the node returns");
        Simulator::Destroy();
    }
};

class BatmanFlowMonitorTestCase : public TestCase
{
  public:
    BatmanFlowMonitorTestCase()
        : TestCase("BATMAN-IV chain FlowMonitor metrics across ten seeds")
    {
    }

  private:
    void DoRun() override
    {
        std::vector<double> pdrs;
        std::vector<double> delays;
        for (uint32_t seed = 1; seed <= 10; ++seed)
        {
            const FlowMetrics metrics = RunFlowTrial(seed);
            NS_TEST_ASSERT_MSG_EQ(metrics.txPackets > 0, true, "FlowMonitor observed client packets");
            NS_TEST_ASSERT_MSG_EQ(metrics.rxPackets > 0, true, "chain delivered packets");
            NS_TEST_ASSERT_MSG_EQ(std::isfinite(metrics.meanDelaySeconds), true,
                                  "mean delay is finite");
            pdrs.push_back(metrics.pdr);
            delays.push_back(metrics.meanDelaySeconds);
        }
        const auto [meanPdr, spreadPdr] = MeanAndSpread(pdrs);
        const auto [meanDelay, spreadDelay] = MeanAndSpread(delays);
        std::cout << "BATMAN FlowMonitor over 10 seeds: PDR mean=" << meanPdr
                  << " spread=" << spreadPdr << " delay_s mean=" << meanDelay
                  << " spread=" << spreadDelay << '\n';
    }
};

class BatmanTestSuite : public TestSuite
{
  public:
    BatmanTestSuite()
        : TestSuite("batman", Type::UNIT)
    {
        AddTestCase(new BatmanHeaderTestCase, TestCase::Duration::QUICK);
        AddTestCase(new BatmanSequenceWrapTestCase, TestCase::Duration::QUICK);
        AddTestCase(new BatmanReplayTestCase, TestCase::Duration::QUICK);
        AddTestCase(new BatmanAsymmetricLinkTestCase, TestCase::Duration::QUICK);
        AddTestCase(new BatmanLossyPathTestCase, TestCase::Duration::QUICK);
        AddTestCase(new BatmanFailoverBoundTestCase, TestCase::Duration::EXTENSIVE);
        AddTestCase(new BatmanMobilityTestCase, TestCase::Duration::EXTENSIVE);
        AddTestCase(new BatmanFlowMonitorTestCase, TestCase::Duration::EXTENSIVE);
    }
};

static BatmanTestSuite g_batmanTestSuite;
} // namespace
} // namespace ns3
