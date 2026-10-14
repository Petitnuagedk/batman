#ifndef BATMAN_HEADER_H
#define BATMAN_HEADER_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"

#include <cstdint>

namespace ns3
{

/**
 * @brief Control message for the proposed IPv4 BATMAN module.
 *
 * The module-specific wire format is not compatible with Linux batman-adv.
 */
class BatmanHeader : public Header
{
  public:
    /** Message type identifiers supported by this header. */
    enum MessageType : uint8_t
    {
        OGM = 1 ///< Originator Message.
    };

    BatmanHeader();

    /** @copydoc Header::GetTypeId() */
    static TypeId GetTypeId();
    /** @copydoc Header::GetInstanceTypeId() */
    TypeId GetInstanceTypeId() const override;
    /**
     * Print a readable representation of this header.
     *
     * @param os Stream to which the header is written.
     */
    void Print(std::ostream& os) const override;
    /**
     * Get the serialized header size.
     *
     * @return Header size in bytes.
     */
    uint32_t GetSerializedSize() const override;
    /**
     * Serialize this header into a packet buffer.
     *
     * @param start Buffer iterator positioned at the start of the header.
     */
    void Serialize(Buffer::Iterator start) const override;
    /**
     * Deserialize this header from a packet buffer.
     *
     * @param start Buffer iterator positioned at the start of the header.
     * @return Number of bytes consumed.
     */
    uint32_t Deserialize(Buffer::Iterator start) override;

    /**
     * Set the IPv4 address that originated this message.
     *
     * @param address Originator IPv4 address.
     */
    void SetOriginator(Ipv4Address address);
    /**
     * Get the originator IPv4 address.
     *
     * @return Originator IPv4 address.
     */
    Ipv4Address GetOriginator() const;
    /**
     * Set this message's sequence number.
     *
     * @param sequence Originator sequence number.
     */
    void SetSequence(uint32_t sequence);
    /**
     * Get this message's sequence number.
     *
     * @return Originator sequence number.
     */
    uint32_t GetSequence() const;
    /**
     * Set the propagated route-quality value.
     *
     * @param tq Route-quality value.
     */
    void SetTq(uint8_t tq);
    /**
     * Get the propagated route-quality value.
     *
     * @return Route-quality value.
     */
    uint8_t GetTq() const;
    /**
     * Set the remaining message lifetime.
     *
     * @param ttl Time-to-live value.
     */
    void SetTtl(uint8_t ttl);
    /**
     * Get the remaining message lifetime.
     *
     * @return Time-to-live value.
     */
    uint8_t GetTtl() const;
    /**
     * Set the control message type.
     *
     * @param type Message type.
     */
    void SetType(MessageType type);
    /**
     * Get the control message type.
     *
     * @return Message type.
     */
    MessageType GetType() const;

  private:
    uint8_t m_version;        ///< Wire-format version.
    MessageType m_type;       ///< Control message type.
    uint8_t m_ttl;            ///< Remaining flood lifetime.
    uint8_t m_tq;             ///< Simplified propagated route-quality value.
    Ipv4Address m_originator; ///< Originator IPv4 address.
    uint32_t m_sequence;      ///< Originator sequence number.
};

} // namespace ns3

#endif // BATMAN_HEADER_H
