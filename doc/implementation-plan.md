# Implementation and validation notes

## First milestone implemented

The module implements a proactive IPv4 routing skeleton with OGM flooding. Each node periodically emits a module-specific UDP broadcast. A receiver records the sender as the next hop to the OGM originator, suppresses duplicate originator/sequence pairs, and floods the OGM with decremented TTL. Learned routes expire after `RouteTimeout`.

## Important model simplifications

- The protocol is BATMAN-inspired, not an exact implementation of a published version or Linux batman-adv.
- The custom header is 12 bytes and has no interoperability guarantee.
- OGM link-quality values are not yet estimated from packet delivery ratios. Route selection currently emphasizes sequence freshness, then the received TQ field.
- Duplicate state is bounded by coarse cache clearing, not per-entry aging.
- The example uses a static chain and is an initial smoke test; it does not yet automate mobility recovery assertions.

## Acceptance checks

1. Build the module against the target ns-3 release.
2. Run `./test.py -s batman` and require the header round-trip test to pass.
3. Run `./ns3 run "batman-chain"`; require nonzero server receive count and inspect printed routes.
4. Confirm global routing is not installed and the endpoints are outside each other's propagation range.
5. Repeat with fixed seeds and a topology change before using results for research.
