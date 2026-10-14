#include "batman-header.h"
#include "ns3/log.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE("BatmanHeader");
NS_OBJECT_ENSURE_REGISTERED(BatmanHeader);

BatmanHeader::BatmanHeader()
  : m_version(1), m_type(OGM), m_ttl(50), m_tq(255),
    m_originator(Ipv4Address::GetZero()), m_sequence(0) {}

TypeId BatmanHeader::GetTypeId()
{
  static TypeId tid = TypeId("ns3::BatmanHeader")
    .SetParent<Header>().SetGroupName("Batman")
    .AddConstructor<BatmanHeader>();
  return tid;
}
TypeId BatmanHeader::GetInstanceTypeId() const { return GetTypeId(); }
void BatmanHeader::Print(std::ostream& os) const
{
  os << "type=" << unsigned(m_type) << " originator=" << m_originator
     << " seq=" << m_sequence << " tq=" << unsigned(m_tq)
     << " ttl=" << unsigned(m_ttl);
}
uint32_t BatmanHeader::GetSerializedSize() const { return 12; }
void BatmanHeader::Serialize(Buffer::Iterator i) const
{
  i.WriteU8(m_version); i.WriteU8(static_cast<uint8_t>(m_type));
  i.WriteU8(m_ttl); i.WriteU8(m_tq); i.WriteHtonU32(m_originator.Get());
  i.WriteHtonU32(m_sequence);
}
uint32_t BatmanHeader::Deserialize(Buffer::Iterator i)
{
  m_version=i.ReadU8(); m_type=static_cast<MessageType>(i.ReadU8());
  m_ttl=i.ReadU8(); m_tq=i.ReadU8();
  m_originator=Ipv4Address(i.ReadNtohU32()); m_sequence=i.ReadNtohU32();
  return GetSerializedSize();
}
void BatmanHeader::SetOriginator(Ipv4Address a) { m_originator=a; }
Ipv4Address BatmanHeader::GetOriginator() const { return m_originator; }
void BatmanHeader::SetSequence(uint32_t s) { m_sequence=s; }
uint32_t BatmanHeader::GetSequence() const { return m_sequence; }
void BatmanHeader::SetTq(uint8_t q) { m_tq=q; }
uint8_t BatmanHeader::GetTq() const { return m_tq; }
void BatmanHeader::SetTtl(uint8_t t) { m_ttl=t; }
uint8_t BatmanHeader::GetTtl() const { return m_ttl; }
void BatmanHeader::SetType(MessageType t) { m_type=t; }
BatmanHeader::MessageType BatmanHeader::GetType() const { return m_type; }
} // namespace ns3
