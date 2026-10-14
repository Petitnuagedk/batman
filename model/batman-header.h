#ifndef BATMAN_HEADER_H
#define BATMAN_HEADER_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/mac48-address.h"
#include "ns3/nstime.h"
#include <cstdint>

namespace ns3 {

/** Control message for the proposed IPv4 BATMAN module.
 * Wire format is module-specific; it is not Linux batman-adv compatible.
 */
class BatmanHeader : public Header
{
  public:
    enum MessageType : uint8_t { OGM = 1 };
    BatmanHeader();
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    void Print(std::ostream& os) const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;

    void SetOriginator(Ipv4Address address);
    Ipv4Address GetOriginator() const;
    void SetSequence(uint32_t sequence);
    uint32_t GetSequence() const;
    void SetTq(uint8_t tq);
    uint8_t GetTq() const;
    void SetTtl(uint8_t ttl);
    uint8_t GetTtl() const;
    void SetType(MessageType type);
    MessageType GetType() const;

  private:
    uint8_t m_version;
    MessageType m_type;
    uint8_t m_ttl;
    uint8_t m_tq;
    Ipv4Address m_originator;
    uint32_t m_sequence;
};

} // namespace ns3
#endif
