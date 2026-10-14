#include "batman-header.h"

#include "ns3/log.h"

namespace ns3
{
NS_LOG_COMPONENT_DEFINE("BatmanHeader");
NS_OBJECT_ENSURE_REGISTERED(BatmanHeader);

BatmanHeader::BatmanHeader()
    : m_version(VERSION),
    m_flags(0),
    m_ttl(50),
    m_gatewayFlags(0),
    m_sequence(0),
    m_gatewayPort(0),
    m_originator(Ipv4Address::GetZero())
{
}

TypeId
BatmanHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::BatmanHeader")
                            .SetParent<Header>()
                            .SetGroupName("Batman")
                            .AddConstructor<BatmanHeader>();
    return tid;
}

TypeId
BatmanHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
BatmanHeader::Print(std::ostream& os) const
{
        os << "version=" << unsigned(m_version) << " flags=0x" << std::hex
             << unsigned(m_flags) << std::dec << " originator=" << m_originator
             << " seq=" << m_sequence << " ttl=" << unsigned(m_ttl);
}

uint32_t
BatmanHeader::GetSerializedSize() const
{
    return SERIALIZED_SIZE;
}

void
BatmanHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(m_version);
    i.WriteU8(m_flags);
    i.WriteU8(m_ttl);
    i.WriteU8(m_gatewayFlags);
    i.WriteHtonU16(m_sequence);
    i.WriteHtonU16(m_gatewayPort);
    i.WriteHtonU32(m_originator.Get());
}

uint32_t
BatmanHeader::Deserialize(Buffer::Iterator i)
{
    m_version = i.ReadU8();
    m_flags = i.ReadU8();
    m_ttl = i.ReadU8();
    m_gatewayFlags = i.ReadU8();
    m_sequence = i.ReadNtohU16();
    m_gatewayPort = i.ReadNtohU16();
    m_originator = Ipv4Address(i.ReadNtohU32());
    return GetSerializedSize();
}

uint8_t
BatmanHeader::GetVersion() const
{
    return m_version;
}

void
BatmanHeader::SetOriginator(Ipv4Address a)
{
    m_originator = a;
}

Ipv4Address
BatmanHeader::GetOriginator() const
{
    return m_originator;
}

void
BatmanHeader::SetSequence(uint16_t s)
{
    m_sequence = s;
}

uint16_t
BatmanHeader::GetSequence() const
{
    return m_sequence;
}

void
BatmanHeader::SetTtl(uint8_t t)
{
    m_ttl = t;
}

uint8_t
BatmanHeader::GetTtl() const
{
    return m_ttl;
}

void
BatmanHeader::SetGatewayFlags(uint8_t flags)
{
    m_gatewayFlags = flags;
}

uint8_t
BatmanHeader::GetGatewayFlags() const
{
    return m_gatewayFlags;
}

void
BatmanHeader::SetGatewayPort(uint16_t port)
{
    m_gatewayPort = port;
}

uint16_t
BatmanHeader::GetGatewayPort() const
{
    return m_gatewayPort;
}

void
BatmanHeader::SetUnidirectional(bool unidirectional)
{
    m_flags = unidirectional ? m_flags | UNIDIRECTIONAL_FLAG
                             : m_flags & static_cast<uint8_t>(~UNIDIRECTIONAL_FLAG);
}

bool
BatmanHeader::IsUnidirectional() const
{
    return (m_flags & UNIDIRECTIONAL_FLAG) != 0;
}

void
BatmanHeader::SetDirectLink(bool directLink)
{
    m_flags = directLink ? m_flags | DIRECT_LINK_FLAG
                         : m_flags & static_cast<uint8_t>(~DIRECT_LINK_FLAG);
}

bool
BatmanHeader::IsDirectLink() const
{
    return (m_flags & DIRECT_LINK_FLAG) != 0;
}
} // namespace ns3
