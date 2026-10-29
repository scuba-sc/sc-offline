# Contracts SDK boundary

Trusted native, command-first external consumer of `game.missions` 1.0.
Build this directory against an extracted SDK containing `sc_missions.h`.
The public service and sco-core provider implementation exist, but production
mutations remain gated on a positively admitted, ready backend. Unavailable or
unadmitted providers refuse; no private engine fallback is compiled.
`contracts.status` is retained; `contracts.offer <index>` and
`contracts.mission <id>` expose copied records. IDs accepted by commands are
positive signed 64-bit integers. See `../../docs/contracts-sdk.md` for limitations.
