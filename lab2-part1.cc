#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/error-model.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/trace-helper.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Lab2Part1");

static Ptr<OutputStreamWrapper> gCwndStream;
static bool gFirstCwnd = true;

static void
CwndTracer(std::string /*context*/, uint32_t oldval, uint32_t newval)
{
    if (gFirstCwnd)
    {
        *gCwndStream->GetStream() << "0.0 " << oldval << std::endl;
        gFirstCwnd = false;
    }
    *gCwndStream->GetStream() << Simulator::Now().GetSeconds() << " " << newval << std::endl;
}

int
main(int argc, char* argv[])
{
    // Parâmetros do terminal
    std::string dataRate = "10Mbps";
    std::string delay = "100ms";
    double errorRate = 1e-5;
    uint32_t nFlows = 1;
    std::string transportProt = "TcpCubic";
    double simTime = 20.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("dataRate", "dataRate", dataRate);
    cmd.AddValue("delay", "delay", delay);
    cmd.AddValue("errorRate", "errorRate", errorRate);
    cmd.AddValue("nFlows", "nFlows", nFlows);
    cmd.AddValue("transport_prot", "transport_prot", transportProt);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName("ns3::" + transportProt)));
    // Topologia
    NodeContainer nodes;
    nodes.Create(4);
    Ptr<Node> source = nodes.Get(0);
    Ptr<Node> r1 = nodes.Get(1);
    Ptr<Node> r2 = nodes.Get(2);
    Ptr<Node> dest = nodes.Get(3);

    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    access.SetChannelAttribute("Delay", StringValue("0.01ms"));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(dataRate));
    bottleneck.SetChannelAttribute("Delay", StringValue(delay));

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(errorRate));

    NetDeviceContainer d_s_r1 = access.Install(source, r1);
    NetDeviceContainer d_r1_r2 = bottleneck.Install(r1, r2);
    NetDeviceContainer d_r2_d = access.Install(r2, dest);
    d_r1_r2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    InternetStackHelper stack;
    stack.InstallAll();

    Ipv4AddressHelper addr;
    Ipv4InterfaceContainer i_s_r1, i_r1_r2, i_r2_d;

    addr.SetBase("10.1.1.0", "255.255.255.0");
    i_s_r1 = addr.Assign(d_s_r1);

    addr.SetBase("10.1.2.0", "255.255.255.0");
    i_r1_r2 = addr.Assign(d_r1_r2);

    addr.SetBase("10.1.3.0", "255.255.255.0");
    i_r2_d = addr.Assign(d_r2_d);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Ipv4Address destAddr = i_r2_d.GetAddress(1);

    uint16_t basePort = 5000;
    ApplicationContainer sinks, sourcesApps;

    for (uint32_t k = 0; k < nFlows; ++k)
    {
        uint16_t port = basePort + k;

        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        sinks.Add(sink.Install(dest));

        BulkSendHelper src("ns3::TcpSocketFactory",
                           Address(InetSocketAddress(destAddr, port)));
        src.SetAttribute("MaxBytes", UintegerValue(0)); // envia indefinidamente
        sourcesApps.Add(src.Install(source));
    }

    double startSink = 0.0;
    double startSrc = 1.0;

    sinks.Start(Seconds(startSink));
    sinks.Stop(Seconds(simTime));
    sourcesApps.Start(Seconds(startSrc));
    sourcesApps.Stop(Seconds(simTime));

    FlowMonitorHelper fmHelper;
    Ptr<FlowMonitor> monitor = fmHelper.InstallAll();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(fmHelper.GetClassifier());

    {
        AsciiTraceHelper ascii;
        std::string cwndFile = std::string("cwnd_") + transportProt + ".txt";
        gCwndStream = ascii.CreateFileStream(cwndFile);
        gFirstCwnd = true;

        uint32_t senderNodeId = source->GetId();
        Simulator::Schedule(Seconds(startSrc + 0.01), [senderNodeId]() {
            std::string path = "/NodeList/" + std::to_string(senderNodeId) +
                               "/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow";
            Config::Connect(path, MakeCallback(&CwndTracer));
        });
    }
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    auto stats = monitor->GetFlowStats();

    double activeTime = std::max(1e-9, simTime - startSrc);
    double sumGoodput = 0.0;

    std::ostringstream out;
    out << "Resultados - " << transportProt << "\n"
        << "----------------------------------\n";

    for (const auto& kv : stats)
    {
        FlowId id = kv.first;
        const FlowMonitor::FlowStats& st = kv.second;

        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(id);

        if (t.destinationAddress != destAddr)
            continue;

        double goodputMbps = (st.rxBytes * 8.0) / activeTime / 1e6;
        sumGoodput += goodputMbps;

        out << "Fluxo " << id << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n"
            << "  RxBytes: " << st.rxBytes << "\n"
            << "  Goodput: " << std::fixed << std::setprecision(6) << goodputMbps << " Mbps\n"
            << "----------------------------------\n";
    }

    out << "Somatório Goodput: " << std::fixed << std::setprecision(6) << sumGoodput << " Mbps\n";

    std::ofstream f("lab2_part1_output.txt");
    f << out.str();
    f.close();

    std::cout << out.str();

    Simulator::Destroy();
    return 0;
}
