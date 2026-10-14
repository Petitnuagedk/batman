#include "ns3/applications-module.h"
#include "ns3/batman-helper.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

using namespace ns3;

namespace
{
struct TrialMetrics
{
    uint64_t txPackets{0};
    uint64_t rxPackets{0};
    double pdr{0.0};
    double delaySeconds{0.0};
};

TrialMetrics
RunChain(uint32_t seed, double stopTime, double spacing, double range)
{
    RngSeedManager::SetSeed(seed);
    NodeContainer nodes;
    nodes.Create(5);
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(range));
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
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        positions->Add(Vector(index * spacing, 0, 0));
    }
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    BatmanHelper routing;
    InternetStackHelper internet;
    internet.SetRoutingHelper(routing);
    internet.Install(nodes);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.1.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = addresses.Assign(devices);

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();
    constexpr uint16_t port = 9000;
    UdpServerHelper server(port);
    ApplicationContainer serverApps = server.Install(nodes.Get(4));
    serverApps.Start(Seconds(0.5));
    serverApps.Stop(Seconds(stopTime));
    UdpClientHelper client(interfaces.GetAddress(4), port);
    client.SetAttribute("MaxPackets", UintegerValue(100000));
    client.SetAttribute("Interval", TimeValue(MilliSeconds(250)));
    client.SetAttribute("PacketSize", UintegerValue(512));
    ApplicationContainer clientApps = client.Install(nodes.Get(0));
    clientApps.Start(Seconds(5.0));
    clientApps.Stop(Seconds(stopTime - 0.1));

    Simulator::Stop(Seconds(stopTime));
    Simulator::Run();
    monitor->CheckForLostPackets();

    TrialMetrics metrics;
    Time totalDelay;
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    for (const auto& flow : monitor->GetFlowStats())
    {
        if (classifier->FindFlow(flow.first).destinationPort == port)
        {
            metrics.txPackets += flow.second.txPackets;
            metrics.rxPackets += flow.second.rxPackets;
            totalDelay += flow.second.delaySum;
        }
    }
    if (metrics.txPackets != 0)
    {
        metrics.pdr = static_cast<double>(metrics.rxPackets) / metrics.txPackets;
    }
    if (metrics.rxPackets != 0)
    {
        metrics.delaySeconds = totalDelay.GetSeconds() / metrics.rxPackets;
    }

    Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(&std::cout);
    std::cout << "Seed " << seed << " routing tables at " << Simulator::Now().GetSeconds()
              << "s\n";
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        std::cout << "Node " << index << '\n';
        nodes.Get(index)->GetObject<Ipv4>()->GetRoutingProtocol()->PrintRoutingTable(stream);
    }
    Simulator::Destroy();
    return metrics;
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
} // namespace

/**
 * Run a five-node ad hoc Wi-Fi chain over ten RNG seeds.
 *
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument values.
 * @return Zero if all runs deliver application traffic; otherwise, 2.
 */
int
main(int argc, char** argv)
{
    double stopTime = 15.0;
    double spacing = 50.0;
    double range = 55.0;
    uint32_t seeds = 10;
    CommandLine cmd(__FILE__);
    cmd.AddValue("stopTime", "Simulation duration in seconds", stopTime);
    cmd.AddValue("spacing", "Distance between adjacent nodes in metres", spacing);
    cmd.AddValue("range", "Radio propagation range in metres", range);
    cmd.AddValue("seeds", "Number of RNG seeds to evaluate", seeds);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(seeds == 0, "At least one seed is required");
    std::vector<double> pdrs;
    std::vector<double> delays;
    pdrs.reserve(seeds);
    delays.reserve(seeds);
    for (uint32_t seed = 1; seed <= seeds; ++seed)
    {
        const TrialMetrics metrics = RunChain(seed, stopTime, spacing, range);
        std::cout << "seed=" << seed << " tx=" << metrics.txPackets
                  << " rx=" << metrics.rxPackets << " pdr=" << metrics.pdr
                  << " mean_delay_s=" << metrics.delaySeconds << '\n';
        NS_ABORT_MSG_IF(metrics.txPackets == 0, "FlowMonitor observed no application packets");
        NS_ABORT_MSG_IF(metrics.rxPackets == 0, "No application packets reached the server");
        NS_ABORT_MSG_IF(!std::isfinite(metrics.delaySeconds), "Mean delay is not finite");
        pdrs.push_back(metrics.pdr);
        delays.push_back(metrics.delaySeconds);
    }

    const auto [meanPdr, spreadPdr] = MeanAndSpread(pdrs);
    const auto [meanDelay, spreadDelay] = MeanAndSpread(delays);
    std::cout << "BATMAN chain FlowMonitor summary over " << seeds << " seeds\n"
              << "PDR mean=" << meanPdr << " spread=" << spreadPdr << '\n'
              << "mean_delay_s mean=" << meanDelay << " spread=" << spreadDelay << '\n';
    return 0;
}
