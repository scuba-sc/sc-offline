#pragma once

// One version for the DLL and the launcher. Shown in mod.log, the menu title and the launcher.
// sc-offline is based on ChrisWareOffline 0.9.0-rc1 (GPL-3.0); see CHANGELOG.md.
// Keep SCO_VERSION and the three numbers below in step (the .rc version block reads the numbers).
// A release candidate has a "-rc.<n>" suffix on SCO_VERSION, the same three numbers as the final release and
// SCO_VERSION_PRERELEASE defined; the final release drops both. The release tag is v<SCO_VERSION>.
#define SCO_VERSION_MAJOR 0
#define SCO_VERSION_MINOR 8
#define SCO_VERSION_PATCH 0
#define SCO_VERSION  "0.8.0-rc.2"
#define SCO_VERSION_PRERELEASE 1
#define SCO_TITLE    "sc-offline v" SCO_VERSION
#define SCO_BASED_ON "based on ChrisWareOffline 0.9.0-rc1"
