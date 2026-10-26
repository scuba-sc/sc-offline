// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#define SCO_LAUNCHER_MESSAGES(X) \
    X(Play, "launcher.action.play", "Play") \
    X(Running, "launcher.action.running", "Running {command}") \
    X(FormatUnavailable, "launcher.text.unavailable", "Text could not be displayed.") \
    X(Status, "launcher.action.status", "Status") \
    X(Update, "launcher.action.update", "Update") \
    X(Install, "launcher.action.install", "Install") \
    X(Uninstall, "launcher.action.uninstall", "Uninstall") \
    X(Yes, "launcher.action.yes", "Yes") \
    X(No, "launcher.action.no", "No") \
    X(Copy, "launcher.action.copy", "Copy") \
    X(Reload, "launcher.action.reload", "Reload") \
    X(TurnOn, "launcher.action.turn_on", "Turn on") \
    X(OpenFolder, "launcher.action.open_folder", "Open folder") \
    X(Back, "launcher.action.back", "Back") \
    X(Save, "launcher.action.save", "Save") \
    X(Reset, "launcher.action.reset", "Reset") \
    X(Launcher, "launcher.heading.launcher", "Launcher") \
    X(StatusHeading, "launcher.heading.status", "Status") \
    X(Actions, "launcher.heading.actions", "Actions") \
    X(Plugins, "launcher.heading.plugins", "Plugins") \
    X(Output, "launcher.heading.output", "Output") \
    X(LeftMod, "launcher.status.left_mod", "the mod is still in Bin64") \
    X(LeftFirewall, "launcher.status.left_firewall", "firewall rule \"{name}\"") \
    X(LeftHosts, "launcher.status.left_hosts", "the EAC line in the hosts file") \
    X(LeftEac, "launcher.status.left_eac", "EasyAntiCheat_EOS.exe renamed to .bak") \
    X(UnsafeRunning, "launcher.status.unsafe_running", "Not safe to go online") \
    X(RunningDetail, "launcher.status.running_detail", "Star Citizen is running. When it closes, sc-offline takes the mod out and undoes its PC changes.") \
    X(UnsafeLeftovers, "launcher.status.unsafe_leftovers", "Not safe to go online yet") \
    X(UndoDetail, "launcher.status.undo_detail", "Click Uninstall to undo: {items}") \
    X(UnknownFolder, "launcher.status.unknown_folder", "Game folder not known yet") \
    X(UnknownFolderDetail, "launcher.status.unknown_folder_detail", "Click Status or Play to find it. No PC changes are left over.") \
    X(SafeOnline, "launcher.status.safe_online", "Safe to go online") \
    X(SafeDetail, "launcher.status.safe_detail", "The mod is out of the game folder and no PC changes are left.") \
    X(DiscordHint, "launcher.hint.discord", "Show \"Playing sc-offline\" on your Discord profile from the next Play") \
    X(LogsFolder, "launcher.hint.logs_folder", "Logs folder") \
    X(Settings, "launcher.hint.settings", "Settings") \
    X(PluginsFolder, "launcher.hint.plugins_folder", "Open the plugins folder") \
    X(HotReloadNote, "launcher.plugins.hot_reload_note", "Hot reload runs in the game: use the sco.reload command there. The launcher can't call it.") \
    X(HotReloadHint, "launcher.plugins.hot_reload_hint", "Plugins reload inside the running game (sco.reload)") \
    X(Search, "launcher.hint.search", "Search") \
    X(PluginsOff, "launcher.plugins.off", "Plugins are off (plugins = off in sc-offline.ini)") \
    X(NoPlugins, "launcher.plugins.empty", "No plugins yet") \
    X(NoMatches, "launcher.plugins.no_matches", "No matches") \
    X(Off, "launcher.plugins.state_off", "Off") \
    X(On, "launcher.plugins.state_on", "On") \
    X(PluginStateNote, "launcher.plugins.state_note", "{state}: {plugin}. The game picks this up at its next start.") \
    X(PluginDisabledError, "launcher.plugins.disabled_error", "Couldn't change data\\plugins\\{plugin}\\disabled (is the folder read-only?)") \
    X(PluginKind, "launcher.plugins.kind", "{kind} plugin") \
    X(PluginSettings, "launcher.plugins.settings", "{plugin} settings") \
    X(SettingsBad, "launcher.plugins.settings_bad", "plugin.ini has a [settings] line this launcher can't read; the game may refuse the plugin. Run sco-plugin-check on the folder.") \
    X(SettingsSaved, "launcher.plugins.saved", "Saved. Applies at the next game start.") \
    X(SettingsReset, "launcher.plugins.reset", "Back to the defaults. Applies at the next game start.") \
    X(CloseToEdit, "launcher.plugins.close_to_edit", "Close the game to edit.") \
    X(AppliesNextStart, "launcher.plugins.applies_next_start", "Applies at the next game start.") \
    X(PluginsEnabled, "launcher.plugins.enabled", "Plugins are on. They load the next time the game starts.") \
    X(PluginsEnableError, "launcher.plugins.enable_error", "Couldn't write sc-offline.ini; set plugins = on there by hand.") \
    X(BoolExpected, "launcher.setting.bool_expected", "true or false") \
    X(IntegerExpected, "launcher.setting.integer_expected", "a whole number") \
    X(AtLeast, "launcher.setting.at_least", "at least {number}") \
    X(AtMost, "launcher.setting.at_most", "at most {number}") \
    X(NumberExpected, "launcher.setting.number_expected", "a number such as 1.5") \
    X(FiniteExpected, "launcher.setting.finite_expected", "not a finite number") \
    X(StringLimit, "launcher.setting.string_limit", "at most 255 characters") \
    X(AsciiExpected, "launcher.setting.ascii_expected", "printable ASCII only") \
    X(ChoiceExpected, "launcher.setting.choice_expected", "pick one of the choices") \
    X(OneOf, "launcher.setting.one_of", "one of {choices}") \
    X(MinHint, "launcher.setting.min_hint", ", min {number}") \
    X(MaxHint, "launcher.setting.max_hint", ", max {number}") \
    X(StorageOpenError, "launcher.setting.storage_open_error", "couldn't open data\\storage\\{plugin}.db") \
    X(StorageWriteError, "launcher.setting.storage_write_error", "couldn't write the values: {error}") \
    X(CloseConfirm, "launcher.window.close_confirm", "A command is still running. If you close this window, sc-offline still takes the mod out and undoes its PC changes when the game closes, but any question it asks is answered No.\n\nClose anyway?") \
    X(WindowTitle, "launcher.window.title", "sc-offline {version} - Star Citizen offline mod") \
    X(WindowUnavailable, "launcher.window.unavailable", "sc-offline couldn't open its window (Direct3D 11 isn't available).\n\nRun it from a terminal instead, for example: sc-offline.exe play") \
    X(Language, "launcher.language.label", "Language") \
    X(LanguageEnglish, "launcher.language.english", "English") \
    X(LanguageRussian, "launcher.language.russian", "Russian (ru)") \
    X(LanguageAuto, "launcher.language.auto", "Auto") \
    X(LanguageHint, "launcher.language.hint", "Choose the launcher language. Auto uses the Windows UI language (English or Russian).") \
    X(LanguageMissing, "launcher.language.missing", "Russian override is unreadable or invalid; fix or remove it to use built-in text. English is shown.") \
    X(LanguageGlyphs, "launcher.language.glyphs", "Required font glyphs are unavailable; English is shown.") \
    X(LanguageSaved, "launcher.language.saved", "Language saved.") \
    X(LanguageWriteError, "launcher.language.write_error", "Couldn't write sc-offline.ini; the language choice was kept.") \
    X(LanguageBusy, "launcher.language.busy", "Wait for the command and close the game to change language.") \
    X(ChildStartError, "launcher.output.child_start_error", "couldn't start sc-offline.exe {command}") \
    X(DiscordOn, "launcher.output.discord_on", "Discord status on: your profile shows \"Playing sc-offline\" from the next Play.") \
    X(DiscordOff, "launcher.output.discord_off", "Discord status off: from the next Play, nothing is shown on Discord.") \
    X(DiscordError, "launcher.output.discord_error", "couldn't change data\\plugins\\discord\\disabled (is the folder read-only?)") \
    X(ChildDone, "launcher.output.child_done", "Done.") \
    X(ChildFinished, "launcher.output.child_finished", "Exit code {code}; see above.")
