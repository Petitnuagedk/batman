#include "ns3/test.h"
#include "ns3/packet.h"
#include "../model/batman-header.h"
namespace ns3 {
class BatmanHeaderTestCase : public TestCase {
 public: BatmanHeaderTestCase():TestCase("BATMAN OGM header serializes and deserializes"){}
 private: void DoRun() override {
  BatmanHeader h; h.SetOriginator(Ipv4Address("10.1.2.3")); h.SetSequence(0x12345678); h.SetTq(200); h.SetTtl(42);
  Ptr<Packet> p=Create<Packet>(); p->AddHeader(h);
  NS_TEST_ASSERT_MSG_EQ(p->GetSize(),12,"header must be 12 bytes");
  BatmanHeader d; p->RemoveHeader(d);
  NS_TEST_ASSERT_MSG_EQ(d.GetOriginator(),Ipv4Address("10.1.2.3"),"originator round trip");
  NS_TEST_ASSERT_MSG_EQ(d.GetSequence(),0x12345678u,"sequence round trip");
  NS_TEST_ASSERT_MSG_EQ(d.GetTq(),200,"TQ round trip"); NS_TEST_ASSERT_MSG_EQ(d.GetTtl(),42,"TTL round trip");
  NS_TEST_ASSERT_MSG_EQ(static_cast<uint32_t>(d.GetType()),static_cast<uint32_t>(BatmanHeader::OGM),"type round trip");
 }
};
class BatmanTestSuite:public TestSuite { public: BatmanTestSuite():TestSuite("batman",Type::UNIT){AddTestCase(new BatmanHeaderTestCase,TestCase::Duration::QUICK);} };
static BatmanTestSuite g_batmanTestSuite;
}
