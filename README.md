# BATMAN-IV for ns-3

This contribution is an experimental IPv4 `Ipv4RoutingProtocol` implementation based on the attached `draft-wunderlich-openmesh-manet-routing-00` (April 2008). The source is an expired Internet-Draft, not a finalized RFC or a specification of Linux `batman-adv`. The implementation focuses on OGM origination, duplicate suppression, bidirectional-neighbor validation, window-based route selection, relay, expiry, and route lookup. It is not a complete BATMAN implementation.

## Build and test

The module is intended to be built as part of ns-3 after it has been enabled in the top-level configuration:

```sh
./ns3 configure --enable-examples --enable-tests
./ns3 build batman
./ns3 run 'test-runner --suite=batman --fullness=EXTENSIVE --verbose'
./ns3 run batman-chain
```

The `batman` test suite covers header serialization, 16-bit sequence wrap, duplicate/replay suppression, asymmetric-link rejection, route selection with a lossy first sample, failover, mobility expiry/relearning, and FlowMonitor metrics over ten random seeds. The chain example prints per-seed delivery and delay statistics and route tables; results depend on topology, channel configuration, and random seed.

## Draft-to-implementation limitations and assumptions

The draft is the authority for protocol behavior where it is explicit. Where it is silent or incomplete, the implementation uses the following documented approximations; these must not be interpreted as standardized BATMAN requirements:

- **Link-quality metric:** The draft describes TQ and its use for route selection but does not provide the complete TQ computation, asymmetry calculation, or per-hop penalty formula. This implementation selects a route using the count of sequence numbers observed in a fixed per-neighbor window. It does not claim wire- or metric-level interoperability with another BATMAN implementation.
- **Bidirectional-link timeout:** The draft does not define `BI_LINK_TIMEOUT`. The implementation approximates echo-state expiry using the configured 128-entry sequence window and OGM interval. This is an implementation choice, not a draft constant.
- **TTL:** The draft constrains TTL to a range but does not state a default. The implementation retains the prototype's configured default; relaying decrements TTL and does not relay an exhausted packet.
- **Jitter:** The draft gives maximum delays of 200 ms for originator scheduling and 100 ms for rebroadcast but does not define a probability distribution. Uniform random jitter within these bounds is assumed here.
- **OGM scope:** The implementation handles the draft's OGM fields and IPv4 originator addresses. It does not implement HNA announcements or their network-prefix advertisement/withdrawal behavior.
- **Gateway functionality:** Gateway flags and advertised gateway fields are represented in the header, but gateway discovery policy, gateway selection, and tunneled Internet traffic are not implemented.
- **Addressing and interfaces:** The implementation is IPv4-only and maintains state per interface/neighbor. Multi-address nodes, address changes, interface hot-plugging, and interoperability corner cases are not comprehensively validated.
- **Transport and interoperability:** OGMs are sent with interface-bound UDP sockets using limited broadcast. This supports the tested ns-3 broadcast path; it does not establish interoperability with the draft's other possible link-layer environments or with Linux `batman-adv`.
- **Validation scope:** Automated coverage includes deterministic protocol-unit/topology scenarios and a ten-seed, small-chain FlowMonitor experiment. This is not a broad performance study, formal conformance suite, or evidence of production readiness.

## Status

Experimental research code. Treat all unspecified protocol behavior as provisional, and consult the attached draft and source comments before extending the implementation.
