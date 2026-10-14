BATMAN-inspired IPv4 routing model
==================================

This contributed module is an experimental proactive IPv4 MANET routing
protocol inspired by classic BATMAN. It is not the Linux batman-adv protocol
and is not wire-compatible with it.

The first implementation provides OGM flooding over UDP broadcast, duplicate
suppression, sequence-aware originator entries, route expiry, an IPv4 routing
helper, a Wi-Fi chain example, and a serialization unit test. The OGM header
and route-quality metric are module-specific simplifications.

See ``README.md`` for build instructions and limitations. Before research use,
validate the model against the chosen ns-3 release and add automated convergence,
route-recovery, and performance tests.
