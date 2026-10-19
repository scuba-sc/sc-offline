# Raw actor conditions

SDK-only inspector/HUD foundation. `actor_conditions.read <entity> <stat>` reads
one public `sc_actor_stat` ID (1..29), freshly querying game.actor_stats 1.1.
It reports raw float values; absent entities/records and unavailable providers are
errors, never fabricated zero. Health/wear body-part floats are not injury tiers.
No writes, private headers, offsets, hooks or renderer coupling.

Build standalone with the candidate extracted SDK include directory and checksum
inventory. An older provider can still host the plugin, but command queries refuse
the unavailable 1.1 minor. The official product pin/release staging is unchanged.
Matching scuba-engine candidate supplies synthetic actor entity 256. Actual game
units/ranges/direction and body-part behavior still need joint in-game validation.
