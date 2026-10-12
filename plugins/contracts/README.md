# Contracts SDK boundary

Trusted native, command-first external consumer of `game.missions` 1.0.
Build this directory against an extracted SDK containing `sc_missions.h`.
Missing production providers refuse; no private engine fallback is compiled.
`contracts.status` is retained; `contracts.offer <index>` and
`contracts.mission <id>` expose copied records. IDs accepted by commands are
positive signed 64-bit integers. See `../../docs/contracts-sdk.md` for limitations.
