# ASOP host composition consumer

The terminal catalogue consumer is `src/asop_shell.*`; it uses the public,
game-specific host-source `sco::game::asop_host` interface, never a plugin service.
The shipped SDK 2.0.0 pin does not contain this interface. The staged migration is
opt-in and requires a host dependency containing G10; the published pin is unchanged.

Catalogue input is copied before engine hooks run. Class lists are bounded and
invalid/duplicate input refuses atomically. Product filesystem and `asop` policy
stay in the shell. Readiness bits are installation status, not gameplay evidence.
All game mutations, delivery/store/retrieve, lift and synchronous continuation
ownership stay in the game pack. No G13/session broker dependency.

Windows/MSVC and gameplay NOT VERIFIED. Tests NOT RUN under owner speed order.
