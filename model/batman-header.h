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
    static constexpr uint8_t VERSION = 4;                  ///< Draft-00 compatibility version.
    static constexpr uint8_t UNIDIRECTIONAL_FLAG = 0x80;   ///< Unidirectional-link flag mask.
    static constexpr uint8_t DIRECT_LINK_FLAG = 0x40;      ///< Direct-link flag mask.
    static constexpr uint32_t SERIALIZED_SIZE = 12;        ///< OGM header size in octets.

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
     * Get the BATMAN compatibility version.
     *
     * @return Wire-format version.
     */
    uint8_t GetVersion() const;
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
     * Set this message's 16-bit sequence number.
     *
     * @param sequence Originator sequence number.
     */
    void SetSequence(uint16_t sequence);
    /**
     * Get this message's sequence number.
     *
     * @return Originator sequence number.
     */
    uint16_t GetSequence() const;
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
     * Set the gateway capability flags.
     *
     * @param flags Gateway flags.
     */
    void SetGatewayFlags(uint8_t flags);
    /**
     * Get the gateway capability flags.
     *
     * @return Gateway flags.
     */
    uint8_t GetGatewayFlags() const;
    /**
     * Set the UDP port advertised for gateway tunneling.
     *
     * @param port Gateway UDP port.
     */
    void SetGatewayPort(uint16_t port);
    /**
     * Get the advertised gateway UDP port.
     *
     * @return Gateway UDP port.
     */
    uint16_t GetGatewayPort() const;
    /**
     * Set whether the packet was received via a unidirectional link.
     *
     * @param unidirectional True to set the U flag.
     */
    void SetUnidirectional(bool unidirectional);
    /**
     * Check whether the U flag is set.
     *
     * @return True if the packet is marked unidirectional.
     */
    bool IsUnidirectional() const;
    /**
     * Set whether the packet was received directly from its originator.
     *
     * @param directLink True to set the D flag.
     */
    void SetDirectLink(bool directLink);
    /**
     * Check whether the D flag is set.
     *
     * @return True if the packet is marked as direct-link.
     */
    bool IsDirectLink() const;

  private:
    uint8_t m_version;           ///< BATMAN protocol compatibility version.
    uint8_t m_flags;             ///< U and D flags in the two most significant bits.
    uint8_t m_ttl;               ///< Remaining flood lifetime.
    uint8_t m_gatewayFlags;      ///< Gateway bandwidth capability.
    uint16_t m_sequence;         ///< 16-bit originator sequence number.
    uint16_t m_gatewayPort;      ///< Gateway tunnel UDP port.
    Ipv4Address m_originator;    ///< Originator interface IPv4 address.
};

} // namespace ns3

#endif // BATMAN_HEADER_H
