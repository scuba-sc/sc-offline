# Status: what works

This page says what has been run in the real game and what has not. Read it before you rely on a feature. Other pages link here instead of repeating it.

A build, a mock, a signature match or a test without the game does not count as a game result. Only the list under "Seen in game" does.

## Seen in game

The maintainer ran seven things on game build **4.10.200.58746**, in one or two sessions each. The runs used development packages with the classic launcher (`sc-offline-classic.exe`) and the classic menu. They did not use the 0.8.0 zip. Every binary in the 0.8.0 package (the game DLLs, both launchers and all plugins) was built after those runs, from later sources. The default game DLL is built the same way as the one that ran (classic menu), but it is a new build.

1. **Startup.** The game started with the mod loaded, and the classic menu opened.
2. **Gear.** Equipping items from the Gear page attached them to the character. The game log shows each attachment.
3. **Build.** An item from the Outposts list was placed from the Build page.
4. **Ships.** A ship spawned, the player boarded it, and Flight Ready worked.
5. **Characters.** A character spawned.
6. **Saved spots.** F7 then F8 really moved the player to the saved spot. The game's own check did not report the arrival, so the plugin now also reads the player's position and says "arrived" within 2 m. That fix has not been run in game. Moving while seated in a ship is untested.
7. **Seat event test.** A small test that logs when the player sits down in a ship seat and stands up again recorded both events.

## Ships in 0.8.0 but not run in game

Everything below is in the package and has been checked without the game. None of it has been run in the real game.

**The new launcher** (`sc-offline.exe`):

- **Play.** It has run only against a stand-in program, not the real game. This covers the step that asks Windows for administrator rights, the firewall rules that cut the game's network, the hosts-file line, the Easy Anti-Cheat file rename, and starting the game from that step.
- **The window.** Browse, saving a plugin's settings, Stop, Repair, and the restart after an update.
- **Updating.** The update check has run against GitHub and the file swap against local zips, with rollback. No real update from 0.7.x to 0.8.0 has run.
- **The plugin catalogue.** The commands read a signed list from a local folder only. See "Not in 0.8.0" below.

If the new launcher does not work on your PC, use `sc-offline-classic.exe`. It is the launcher the maintainer's game runs used, and it undoes the PC changes the new one makes ([how](launcher.md#the-classic-launcher)).

**The new menu** (`menu = rust` in `sc-offline.ini`, [Which menu](../README.md#which-menu)). Neither the menu nor the second game DLL it needs has been run in game. That covers opening it with **M**, giving the keyboard and mouse back to the game, resizing, and the "Classic pages" entry. The classic menu is the default and is the one that was seen.

**Capability switches other than gear.** Four switches are on by default and have not run: reading what you wear and carry, changing survival stats, reading four console settings, and native sounds. This build refuses native sounds and logs why. One diagnostic switch is off by default. The gear switch is the one that was run in game and worked. [Capabilities](capabilities.md) lists them all.

**Travel.** The Travel page and `quantum.travel` have not run, including the quantum drive boost and named saved spots. They work only inside the star system you are in.

**Other features.** Crew actions beyond sitting down, NPC commands, removing placed objects, contracts, mining, creative controls, the inspector, ship terminals, hangars and ATC calls, the Squadron 42 page, and gear beyond equipping (weapon attachments and outfits). [Features](features.md) says what each one is for.

**The package.** It was assembled and checked against its file list on a development machine. Your install is its first full test.

## Not in 0.8.0

- **Travel to another planet.** The game lets the mod look up only the place you are already in, so a faraway destination cannot be reached. `quantum.travel` refuses it and says so ([why](features.md#travel-between-places)).
- **A published plugin catalogue.** The launcher can install plugins from a catalogue, but none is published yet, so those commands and the window's Catalogue tab do nothing. You can still add a plugin from a folder or a zip.
- **Multiplayer and the Titanfall and Minecraft links.** They are separate projects and are not in the package.

## Report a problem

[Open an issue](https://github.com/scuba-sc/sc-offline/issues/new/choose) and say which launcher and which menu you used. Include the output of `sc-offline.exe status`, the mod version, the game build, and the files `data/launcher.log`, `data/mod.log` and the game's `Game.log`. Read the logs for personal information before you post them. For a security problem, do not open a public issue; see [SECURITY](../SECURITY.md).

Maintainers: the evidence, the changes since the last in-game run and the open checks are in the [0.8.0 evidence record](handoff/2026-11-18-release-0.8.0-evidence.md).
