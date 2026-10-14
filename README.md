# BATMAN-inspired IPv4 routing for ns-3

**Status: first implementation proposition; not yet build-validated against a particular ns-3 checkout.** This is a native C++ proactive IPv4 routing protocol for experimentation. It is not Linux `batman-adv`, is not wire-compatible with it, and does not implement Ethernet bridging, client translation tables, BLA, VLAN behavior, or the complete Linux TQ/neighbor algorithm.

## Features in this first milestone

- `Ipv4RoutingProtocol` integration and `BatmanHelper`.
- Module-specific 12-byte OGM header carried in UDP broadcast packets.
- Periodic originator advertisements, duplicate suppression, TTL-limited flooding.
- Originator/next-hop table with sequence-number comparison and route expiry.
- Basic direct-subnet routing and IPv4 forwarding integration.
- A five-node ad hoc Wi-Fi chain example and a header serialization test.

The route-quality field is currently a propagated, simplified metric. It is not a faithful implementation of Linux BATMAN's TQ calculation, and route selection primarily uses freshness. This is suitable as an initial ns-3 integration prototype, not for publication-grade protocol comparisons until the metric and failure handling are further validated.

## Integrate

Copy this directory to `<ns-3-root>/contrib/batman`. The module uses the modern ns-3 CMake module layout. Configure and build with the commands supported by your checkout, typically:

```sh
./ns3 configure --enable-examples --enable-tests
./ns3 build
./test.py -s batman
./ns3 run "batman-chain"
```

The example deliberately installs no global routing protocol. It forms a five-node Wi-Fi chain with 50 m spacing and a configurable propagation range (55 m by default), then sends UDP traffic from node 0 to node 4. Exit status 0 means at least one UDP packet reached the server; status 2 means no packet arrived.

Example parameters:

```sh
./ns3 run "batman-chain --stopTime=20 --spacing=45 --range=52"
```

## Current limitations / next work

1. Build and test against a declared ns-3 release; small API differences may need adaptation.
2. Improve route selection using measured forward/reverse delivery ratios and a documented TQ algorithm.
3. Use bounded time-based duplicate-cache expiry rather than coarse clearing.
4. Add multi-interface, address-removal, interface-failure, sequence-wrap, and mobility integration tests.
5. Add FlowMonitor statistics and automated PDR / delay assertions.
6. Validate convergence and route recovery under repeatable topology changes.
7. Add the full Linux batman-adv Layer-2 model only if that is the required research target.

## License

This original prototype is provided for integration and evaluation. Check the licensing requirements of the target ns-3 tree before redistribution. No Linux kernel implementation code is copied into this module.
