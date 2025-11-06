#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/error-model.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Lab2Part2");

struct SimArtifacts
{
    Ptr<FlowMonitor> monitor;
    Ptr<Ipv4FlowClassifier> classifier;
    Ipv4Address dest1Addr;
    Ipv4Address dest2Addr;
};

static SimArtifacts
BuildAndRunOnce(const std::string& dataRate,
                const std::string& delay,
                double errorRate,
                uint32_t nFlows,
                const std::string& transportProt,
                double simTime)
{
    if (nFlows % 2 != 0)
    {
        NS_LOG_WARN("nflows errado");
        nFlows -= 1;
    }
    uint32_t flowsPerDest = nFlows / 2;

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName("ns3::" + transportProt)));

    NodeContainer nodes;
    nodes.Create(5);
    Ptr<Node> source = nodes.Get(0);
    Ptr<Node> r1 = nodes.Get(1);
    Ptr<Node> r2 = nodes.Get(2);
    Ptr<Node> dest1 = nodes.Get(3);
    Ptr<Node> dest2 = nodes.Get(4);

    PointToPointHelper p2pFast;
    p2pFast.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2pFast.SetChannelAttribute("Delay", StringValue("0.01ms"));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(dataRate));
    bottleneck.SetChannelAttribute("Delay", StringValue(delay));

    PointToPointHelper toDest1;
    toDest1.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    toDest1.SetChannelAttribute("Delay", StringValue("0.01ms"));

    PointToPointHelper toDest2;
    toDest2.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    toDest2.SetChannelAttribute("Delay", StringValue("50ms"));

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(errorRate));

    NetDeviceContainer d_src_r1 = p2pFast.Install(source, r1);
    NetDeviceContainer d_r1_r2  = bottleneck.Install(r1, r2);
    NetDeviceContainer d_r2_d1  = toDest1.Install(r2, dest1);
    NetDeviceContainer d_r2_d2  = toDest2.Install(r2, dest2);

    d_r1_r2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    InternetStackHelper stack;
    stack.InstallAll();

    Ipv4AddressHelper addr;
    Ipv4InterfaceContainer i_src_r1, i_r1_r2, i_r2_d1, i_r2_d2;

    addr.SetBase("10.2.1.0", "255.255.255.0");
    i_src_r1 = addr.Assign(d_src_r1);

    addr.SetBase("10.2.2.0", "255.255.255.0");
    i_r1_r2 = addr.Assign(d_r1_r2);

    addr.SetBase("10.2.3.0", "255.255.255.0");
    i_r2_d1 = addr.Assign(d_r2_d1);

    addr.SetBase("10.2.4.0", "255.255.255.0");
    i_r2_d2 = addr.Assign(d_r2_d2);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Ipv4Address dest1Addr = i_r2_d1.GetAddress(1);
    Ipv4Address dest2Addr = i_r2_d2.GetAddress(1);

    uint16_t basePort = 6000;
    ApplicationContainer sinkApps, srcApps;

    for (uint32_t k = 0; k < flowsPerDest; ++k)
    {
        uint16_t port = basePort + k;
        PacketSinkHelper sink1("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
        sinkApps.Add(sink1.Install(dest1));

        PacketSinkHelper sink2("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
        sinkApps.Add(sink2.Install(dest2));
    }

    for (uint32_t k = 0; k < flowsPerDest; ++k)
    {
        uint16_t port = basePort + k;

        Address addr1(InetSocketAddress(dest1Addr, port));
        BulkSendHelper src1("ns3::TcpSocketFactory", addr1);
        src1.SetAttribute("MaxBytes", UintegerValue(0));
        srcApps.Add(src1.Install(nodes.Get(0)));

        Address addr2(InetSocketAddress(dest2Addr, port));
        BulkSendHelper src2("ns3::TcpSocketFactory", addr2);
        src2.SetAttribute("MaxBytes", UintegerValue(0));
        srcApps.Add(src2.Install(nodes.Get(0)));
    }

    double startSink = 0.0;
    double startSrc  = 1.0;

    sinkApps.Start(Seconds(startSink));
    sinkApps.Stop(Seconds(simTime));
    srcApps.Start(Seconds(startSrc));
    srcApps.Stop(Seconds(simTime));

    FlowMonitorHelper fmHelper;
    Ptr<FlowMonitor> monitor = fmHelper.InstallAll();
    Ptr<Ipv4FlowClassifier> ipClassifier =
        DynamicCast<Ipv4FlowClassifier>(fmHelper.GetClassifier());

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    SimArtifacts art;
    art.monitor    = monitor;
    art.classifier = ipClassifier;
    art.dest1Addr  = dest1Addr;
    art.dest2Addr  = dest2Addr;
    return art;
}

int
main(int argc, char* argv[])
{
    std::string dataRate = "1Mbps";
    std::string delay    = "20ms";
    double      errorRate = 1e-5;
    uint32_t    nFlows   = 2;
    std::string transportProt = "TcpCubic";
    double      simTime  = 20.0;
    uint32_t    runs     = 10;
    uint64_t    seed     = 123456789;

    CommandLine cmd(__FILE__);
    cmd.AddValue("dataRate", "dataRate", dataRate);
    cmd.AddValue("delay",    "delay", delay);
    cmd.AddValue("errorRate","errorRate", errorRate);
    cmd.AddValue("nFlows",   "nFlows", nFlows);
    cmd.AddValue("transport_prot", "transport_prot", transportProt);
    cmd.AddValue("runs",     "runs", runs);
    cmd.AddValue("seed",     "seed", seed);
    cmd.Parse(argc, argv);

    if (nFlows < 2 || (nFlows % 2 != 0))
    {
        std::cout << "nflows tem que ser par";
        nFlows = 2;
    }

    RngSeedManager::SetSeed(seed);

    double sumGoodputDest1 = 0.0;
    double sumGoodputDest2 = 0.0;

    std::ostringstream outAll;
    outAll << "Parte 2 - " << transportProt << "\n"
           << "Parâmetros: dataRate=" << dataRate
           << ", delay=" << delay
           << ", errorRate=" << errorRate
           << ", nFlows=" << nFlows
           << ", runs=" << runs << "\n"
           << "----------------------------------\n";

    for (uint32_t run = 0; run < runs; ++run)
    {
        RngSeedManager::SetRun(run + 1);

        SimArtifacts art = BuildAndRunOnce(dataRate, delay, errorRate, nFlows, transportProt, simTime);

        art.monitor->CheckForLostPackets();
        std::map<FlowId, FlowMonitor::FlowStats> stats = art.monitor->GetFlowStats();

        double goodputDest1 = 0.0;
        double goodputDest2 = 0.0;
        uint32_t flowsToD1 = 0;
        uint32_t flowsToD2 = 0;

        double activeTime = std::max(1e-9, simTime - 1.0);

        for (const auto& kv : stats)
        {
            FlowId id = kv.first;
            const FlowMonitor::FlowStats& st = kv.second;
            if (st.rxBytes == 0) continue;

            Ipv4FlowClassifier::FiveTuple t = art.classifier->FindFlow(id);

            double gp = (st.rxBytes * 8.0) / activeTime;

            if (t.destinationAddress == art.dest1Addr)
            {
                goodputDest1 += gp; flowsToD1++;
            }
            else if (t.destinationAddress == art.dest2Addr)
            {
                goodputDest2 += gp; flowsToD2++;
            }
        }

        double avgD1 = (flowsToD1 > 0) ? (goodputDest1 / flowsToD1) : 0.0;
        double avgD2 = (flowsToD2 > 0) ? (goodputDest2 / flowsToD2) : 0.0;

        sumGoodputDest1 += avgD1;
        sumGoodputDest2 += avgD2;

        outAll << "Run " << (run + 1) << ": "
               << "AvgGoodput(dest1)=" << std::fixed << std::setprecision(6) << (avgD1 / 1e6) << " Mbps, "
               << "AvgGoodput(dest2)=" << std::fixed << std::setprecision(6) << (avgD2 / 1e6) << " Mbps\n";

        Simulator::Destroy();
    }

    double finalAvgD1 = sumGoodputDest1 / runs;
    double finalAvgD2 = sumGoodputDest2 / runs;

    outAll << "----------------------------------\n";
    outAll << "Média " << runs << " runs (" << transportProt << "):\n";
    outAll << "  dest1: " << std::fixed << std::setprecision(6) << (finalAvgD1 / 1e6) << " Mbps\n";
    outAll << "  dest2: " << std::fixed << std::setprecision(6) << (finalAvgD2 / 1e6) << " Mbps\n";

    std::ofstream f("lab2_part2_output.txt");
    f << outAll.str();
    f.close();

    std::cout << outAll.str();

    return 0;
}
