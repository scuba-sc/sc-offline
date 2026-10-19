# Contracts SDK boundary

The optional renderer-neutral `contracts.main` panel exposes offer, balance and
stream-radius reads, consumer cancel/resume, and explicit mission accept, read,
objective, advance and abandon actions. Its three argument fields are copied
nonnegative signed 64-bit integers; each command validates its own bounds.
Mission IDs and receipt keys are session-only. Repeated accept keys retain the
provider's once-only semantics. Loading the panel never accepts a mission,
advances progress or pays a reward automatically.

`contracts.radius` uses `game.contracts` 1.0. Missing, short or unavailable
providers refuse without a private fallback or retry. The panel is optional:
commands remain available when widgets are absent or registration is refused.
Unload removes the panel before releasing the consumer's host references.

The dynamic module fixture exercises public synthetic providers against the
published SDK 2.3.0, including explicit widget actions and input refusal. This
is not a rendered-menu or in-game acceptance result. Production mission and
economy availability remains governed by the host; legacy engine ownership is
not retired by this consumer change.

The plugin requires ABI 1.1. The checker's `--short-tables` mode presents ABI
1.0, so loading refuses with `SCO_UNAVAILABLE` before registering commands or
panels. That expected refusal is distinct from a full-host checker pass.

Trusted native, command-first external consumer of `game.missions` 1.0.
Build this directory against an extracted SDK containing `sc_missions.h`.
Missing production providers refuse; no private engine fallback is compiled.
`contracts.status` is retained; `contracts.offer <index>` and
`contracts.mission <id>` expose copied records. IDs accepted by commands are
positive signed 64-bit integers. See `../../docs/contracts-sdk.md` for limitations.
