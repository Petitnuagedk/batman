#include "ns3/applications-module.h"
#include "ns3/batman-helper.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iostream>
using namespace ns3;

/**
 * Run a five-node ad hoc Wi-Fi chain using the BATMAN-inspired routing protocol.
 *
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument values.
 * @return Zero if at least one UDP packet reaches the server; otherwise, 2.
 */
int
main(int argc, char** argv)
{
    double stopTime = 15.0;
    double spacing = 50.0;
    double range = 55.0;
    uint32_t packetSize = 512;
    uint32_t packetIntervalMs = 250;
    CommandLine cmd(__FILE__);
    cmd.AddValue("stopTime", "Simulation duration in seconds", stopTime);
    cmd.AddValue("spacing", "Distance between adjacent nodes in metres", spacing);
    cmd.AddValue("range", "Radio propagation range in metres", range);
    cmd.Parse(argc, argv);

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
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        positions->Add(Vector(i * spacing, 0, 0));
    }
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    BatmanHelper batman;
    InternetStackHelper internet;
    internet.SetRoutingHelper(batman);
    internet.Install(nodes);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.1.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = addresses.Assign(devices);

    uint16_t port = 9000;
    UdpServerHelper server(port);
    ApplicationContainer serverApps = server.Install(nodes.Get(4));
    serverApps.Start(Seconds(0.5));
    serverApps.Stop(Seconds(stopTime));
    UdpClientHelper client(interfaces.GetAddress(4), port);
    client.SetAttribute("MaxPackets", UintegerValue(100000));
    client.SetAttribute("Interval", TimeValue(MilliSeconds(packetIntervalMs)));
    client.SetAttribute("PacketSize", UintegerValue(packetSize));
    ApplicationContainer clientApps = client.Install(nodes.Get(0));
    clientApps.Start(Seconds(5.0));
    clientApps.Stop(Seconds(stopTime - 0.1));

    // Global routing is deliberately not installed: delivery must use BATMAN.
    phy.EnablePcapAll("batman-chain");
    Simulator::Stop(Seconds(stopTime));
    Simulator::Run();
    Ptr<UdpServer> serverPtr = DynamicCast<UdpServer>(serverApps.Get(0));
    uint64_t received = serverPtr->GetReceived();
    std::cout << "BATMAN chain result\n"
              << "nodes=" << nodes.GetN() << " spacing_m=" << spacing << " range_m=" << range
              << "\n"
              << "sent_packets_approx=" << (stopTime - 5.1) * 1000.0 / packetIntervalMs
              << " received_packets=" << received << "\nroute tables:\n";
    Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(&std::cout);
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        std::cout << "Node " << i << "\n";
        nodes.Get(i)->GetObject<Ipv4>()->GetRoutingProtocol()->PrintRoutingTable(stream);
    }
    Simulator::Destroy();
    return received > 0 ? 0 : 2;
}
