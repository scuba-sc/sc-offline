// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;

namespace ScOffline.Desktop
{
    internal static class Guard
    {
        public static bool GameRunning()
        {
            return Process.GetProcessesByName("StarCitizen").Length != 0;
        }
        public static bool NativeRunning()
        {
            return Process.GetProcessesByName("sc-offline").Length != 0;
        }
        private static bool IsCrashReporter(Process process, string game)
        {
            try
            {
                return String.Equals(process.MainModule.FileName,
                                     Path.Combine(game, "Tools", "Public", "CrashHandler.exe"),
                                     StringComparison.OrdinalIgnoreCase);
            }
            catch (InvalidOperationException)
            {
                return false;
            }
        }
        internal static bool CrashReporterRunning(string game)
        {
            foreach (var process in Process.GetProcessesByName("CrashHandler"))
                using (process)
                {
                    if (IsCrashReporter(process, game))
                        return true;
                }
            return false;
        }
        internal static void StopCrashReporters(string game)
        {
            foreach (var process in Process.GetProcessesByName("CrashHandler"))
                using (process)
                {
                    if (!IsCrashReporter(process, game))
                        continue;
                    // A report process can outlive the game. It must not become online when rules leave.
                    try
                    {
                        if (!process.HasExited)
                        {
                            process.Kill();
                            process.WaitForExit(10000);
                        }
                    }
                    catch (InvalidOperationException)
                    { /* The report process already exited. */
                    }
                }
        }
        public static bool Leftovers(string game)
        {
            foreach (string file in new[] { @"Bin64\dinput8.dll", @"Bin64\sc-offline.installed",
                                            @"Bin64\dinput8.dll.sc-offline-backup",
                                            @"user\client\0\default_1.xml.sc-offline-backup" })
                if (File.Exists(Path.Combine(game, file)))
                    return true;
            if (File.Exists(Paths.Eac + ".bak") && !File.Exists(Paths.Eac))
                return true;
            if (File.Exists(Paths.NativeJournal) && new FileInfo(Paths.NativeJournal).Length != 0)
                return true;
            string hosts = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
                                        @"drivers\etc\hosts");
            return File.ReadAllText(hosts).Contains("# added by sc-offline");
        }
        private static bool OwnerAlive(SessionRecord session)
        {
            try
            {
                using (var p = Process.GetProcessById(session.OwnerPid)) return !p.HasExited &&
                    p.StartTime.ToUniversalTime().Ticks == session.OwnerStarted;
            }
            catch (ArgumentException)
            {
                return false;
            }
        }
        private static string[] Executables(string game)
        {
            var paths = new List<string> { Path.Combine(game, "Bin64", "StarCitizen.exe"),
                                           Path.Combine(game, "StarCitizen_Launcher.exe"), Paths.Native };
            foreach (string dir in new[] { Path.Combine(game, "Bin64"), Path.Combine(game, "Tools"),
                                           Path.Combine(game, "EasyAntiCheat"), Paths.Backend })
                if (Directory.Exists(dir))
                    paths.AddRange(Directory.GetFiles(dir, "*.exe", SearchOption.AllDirectories));
            // Find the official launcher beside the game library or through its uninstall entry.
            string parent = Directory.GetParent(game).Parent.FullName;
            string rsi = Path.Combine(parent, "RSI Launcher");
            if (!Directory.Exists(rsi))
                rsi = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                                   "Roberts Space Industries", "RSI Launcher");
            foreach (var hive in new[] { Microsoft.Win32.RegistryHive.LocalMachine,
                                         Microsoft.Win32.RegistryHive.CurrentUser })
                foreach (var view in new[] { Microsoft.Win32.RegistryView.Registry64,
                                             Microsoft.Win32.RegistryView.Registry32 })
                {
                    using (var registry = Microsoft.Win32.RegistryKey.OpenBaseKey(hive, view)) using (
                        var uninstall =
                            registry.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"))
                    {
                        if (uninstall == null)
                            continue;
                        foreach (string name in uninstall.GetSubKeyNames())
                            using (var key = uninstall.OpenSubKey(name))
                            {
                                if (key == null)
                                    continue;
                                string display = key.GetValue("DisplayName") as string;
                                string installed = key.GetValue("InstallLocation") as string;
                                if (display != null &&
                                    display.IndexOf("RSI Launcher", StringComparison.OrdinalIgnoreCase) >=
                                        0 &&
                                    !String.IsNullOrWhiteSpace(installed) &&
                                    File.Exists(Path.Combine(installed, "RSI Launcher.exe")))
                                    paths.AddRange(
                                        Directory.GetFiles(installed, "*.exe", SearchOption.AllDirectories));
                            }
                    }
                }
            if (Directory.Exists(rsi))
                paths.AddRange(Directory.GetFiles(rsi, "*.exe", SearchOption.AllDirectories));
            if (File.Exists(Paths.Eac))
                paths.Add(Paths.Eac);
            return paths.Where(File.Exists).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        }
        public static void StopGame()
        {
            foreach (var process in Process.GetProcessesByName("StarCitizen"))
                using (process)
                {
                    process.CloseMainWindow();
                    if (!process.WaitForExit(10000))
                    {
                        process.Kill();
                        process.WaitForExit(15000);
                    }
                }
        }
        public static int RunNative(string command, string game, Action<string> output)
        {
            var info = new ProcessStartInfo(Paths.Native, command + " --game " + Program.Quote(game)) {
                UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = Paths.Backend,
                RedirectStandardOutput = true, RedirectStandardError = true
            };
            using (var process = new Process { StartInfo = info })
            {
                process.OutputDataReceived += (s, e) =>
                {
                    if (e.Data != null)
                        output(e.Data);
                };
                process.ErrorDataReceived += (s, e) =>
                {
                    if (e.Data != null)
                        output(e.Data);
                };
                process.Start();
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();
                process.WaitForExit();
                return process.ExitCode;
            }
        }
        private static void Save(SessionRecord session)
        {
            Json.Write(Paths.Journal, session);
        }
        private static void Finish(SessionRecord session, Firewall firewall)
        {
            if (GameRunning() || NativeRunning())
                throw new InvalidOperationException(
                    "The game or runtime is still running. Session protection has been kept.");
            session.Phase = "Cleaning";
            Save(session);
            SessionPolicy.CompleteCleanup(
                () =>
                {
                    StopCrashReporters(session.Game);
                    if (Leftovers(session.Game))
                    {
                        int code = RunNative("uninstall", session.Game, line => Program.Log(line));
                        if (code != 0 || Leftovers(session.Game))
                            throw new InvalidOperationException(
                                "The mod could not be cleaned up. Protection remains active. Use Restore " +
                                "online in the launcher.");
                    }
                    // Preserve an older backup left by EAC itself; the native helper otherwise overwrites it.
                    string original = Path.Combine(Paths.Session(session.Id), "eac-original.bak");
                    if (session.HadEacBackup && File.Exists(original))
                    {
                        if (File.Exists(Paths.Eac + ".bak") &&
                            Paths.Hash(Paths.Eac + ".bak") != Paths.Hash(original))
                            throw new InvalidOperationException(
                                "An unexpected EAC backup needs review. Protection remains active.");
                        if (!File.Exists(Paths.Eac + ".bak"))
                            File.Copy(original, Paths.Eac + ".bak");
                    }
                },
                () => SessionPolicy.MayRelease(GameRunning(), NativeRunning(), Leftovers(session.Game),
                                               CrashReporterRunning(session.Game)),
                () =>
                {
                    foreach (string rule in session.Rules.ToArray())
                    {
                        firewall.Remove(rule);
                        session.Rules.Remove(rule);
                        Save(session);
                    }
                    firewall.RemoveLegacy();
                    session.Phase = "Clean";
                    Save(session);
                    File.WriteAllText(Path.Combine(Paths.Session(session.Id), "finished"), "ok");
                    File.Delete(Paths.Journal);
                });
        }
        public static int Session(string id)
        {
            SessionRecord session = null;
            try
            {
                session = Json.Read<SessionRecord>(Paths.Journal);
                if (session.Id != id || session.Phase != "Requested" || !OwnerAlive(session))
                    throw new InvalidOperationException("Invalid session request.");
                session.Game = Paths.ValidateGame(session.Game);
                Program.VerifyRuntime();
                if (GameRunning() || NativeRunning() || Leftovers(session.Game))
                    throw new InvalidOperationException(
                        "Close the game and use Restore online before starting another session.");
                using (var firewall = new Firewall())
                {
                    firewall.AssertEnabled();
                    if (File.Exists(Paths.Eac + ".bak"))
                    {
                        File.Copy(Paths.Eac + ".bak", Path.Combine(Paths.Session(id), "eac-original.bak"),
                                  false);
                        session.HadEacBackup = true;
                        Save(session);
                    }
                    int index = 0;
                    foreach (string path in Executables(session.Game))
                        foreach (int direction in new[] { 1, 2 })
                        {
                            string name = "sc-offline-desktop-" + id + "-" + index++;
                            // Journal before mutation so an interrupted rule addition can be recovered.
                            session.Rules.Add(name);
                            Save(session);
                            firewall.Add(name, path, direction);
                        }
                    foreach (var process in Process.GetProcessesByName("RSI Launcher"))
                        using (process)
                        {
                            process.Kill();
                        }
                    if (GameRunning())
                        throw new InvalidOperationException(
                            "The game was started during preparation. Close it before recovery.");
                    session.Phase = "Protected";
                    Save(session);
                    File.WriteAllText(Path.Combine(Paths.Session(id), "ready"), "ok");
                    DateTime nextFirewallCheck = DateTime.UtcNow;
                    while (OwnerAlive(session) && !File.Exists(Path.Combine(Paths.Session(id), "done")))
                    {
                        // Crash reporting has no offline identity and must not remain open after a fault.
                        StopCrashReporters(session.Game);
                        Thread.Sleep(250);
                        if (File.Exists(Path.Combine(Paths.Session(id), "cancel")))
                            StopGame();
                        try
                        {
                            if (DateTime.UtcNow >= nextFirewallCheck)
                            {
                                firewall.AssertEnabled();
                                nextFirewallCheck = DateTime.UtcNow.AddSeconds(2);
                            }
                        }
                        catch
                        {
                            StopGame();
                            throw;
                        }
                    }
                    while (GameRunning() || NativeRunning())
                    {
                        StopCrashReporters(session.Game);
                        Thread.Sleep(250);
                    }
                    Finish(session, firewall);
                }
                return 0;
            }
            catch (Exception error)
            {
                Program.Log(error.ToString());
                if (session != null)
                {
                    File.WriteAllText(Path.Combine(Paths.Session(session.Id), "error"), error.Message);
                    // A preparation failure is reversible only if no native launch/mod remains.
                    try
                    {
                        if (SessionPolicy.MayRelease(GameRunning(), NativeRunning(), Leftovers(session.Game)))
                            using (var firewall = new Firewall()) Finish(session, firewall);
                    }
                    catch (Exception cleanup)
                    {
                        Program.Log(cleanup.ToString());
                    }
                }
                return 1;
            }
        }
        public static int Recover()
        {
            if (GameRunning() || NativeRunning())
                throw new InvalidOperationException("Close Star Citizen and the native runtime first.");
            using (var firewall = new Firewall())
            {
                if (File.Exists(Paths.Journal))
                {
                    var session = Json.Read<SessionRecord>(Paths.Journal);
                    if (OwnerAlive(session) && !File.Exists(Path.Combine(Paths.Session(session.Id), "done")))
                        throw new InvalidOperationException(
                            "An offline session is still active in another launcher.");
                    session.Game = Paths.ValidateGame(session.Game);
                    Finish(session, firewall);
                }
                else
                {
                    var settings = Json.Read<Settings>(Paths.Settings);
                    string game = Paths.ValidateGame(settings.Game);
                    if (Leftovers(game) &&
                        (RunNative("uninstall", game, line => Program.Log(line)) != 0 || Leftovers(game)))
                        throw new InvalidOperationException("The native runtime could not restore the game.");
                    firewall.RemoveLegacy();
                }
            }
            return 0;
        }
    }
}
