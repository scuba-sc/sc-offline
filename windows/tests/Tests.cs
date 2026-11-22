// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.IO;
using System.Diagnostics;
using System.Threading;
using ScOffline.Desktop;
using ScOffline.Setup;

namespace ScOffline.Tests
{
    internal static class TestProgram
    {
        private static int count;
        private static void Check(bool condition, string name)
        {
            count++;
            if (!condition)
                throw new Exception(name);
        }
        private static void Reject(Action action, string name)
        {
            try
            {
                action();
            }
            catch (InvalidDataException)
            {
                count++;
                return;
            }
            throw new Exception(name);
        }
        public static int Main(string[] args)
        {
            if (args.Length == 1 && args[0] == "--fixture-reporter")
            {
                Thread.Sleep(30000);
                return 0;
            }
            try
            {
                Check(SessionPolicy.MayRelease(false, false, false),
                      "Clean, stopped sessions release isolation");
                Check(!SessionPolicy.MayRelease(true, false, false), "A running game keeps isolation");
                Check(!SessionPolicy.MayRelease(false, true, false),
                      "A runtime still preparing the game keeps isolation");
                Check(!SessionPolicy.MayRelease(false, false, true), "Failed native cleanup keeps isolation");
                Check(!SessionPolicy.MayRelease(true, true, true),
                      "Owner exit is not permission to remove protection");
                Check(!SessionPolicy.MayRelease(false, false, false, true),
                      "A crash reporter outliving the game keeps isolation");
                bool released = false;
                try
                {
                    SessionPolicy.CompleteCleanup(() =>
                                                  { throw new IOException("Injected restore failure"); },
                                                  () => true, () => released = true);
                }
                catch (IOException)
                {
                }
                Check(!released, "Restore exception keeps real release action from running");
                try
                {
                    SessionPolicy.CompleteCleanup(() =>
                                                  {},
                                                  () => false, () => released = true);
                }
                catch (InvalidOperationException)
                {
                }
                Check(!released, "Remaining artifacts prevent release action");
                string order = "";
                SessionPolicy.CompleteCleanup(() => order += "restore;",
                                              () =>
                                              {
                                                  order += "verify;";
                                                  return true;
                                              },
                                              () => order += "release;");
                Check(order == "restore;verify;release;",
                      "Release occurs only after restoration and verification");
                ulong gib = 1024UL * 1024 * 1024;
                Check(SessionPolicy.HasEnoughCommit(36 * gib, false), "Stanton accepts available headroom");
                Check(!SessionPolicy.HasEnoughCommit(36 * gib, true),
                      "All-systems launch requires more headroom");
                Check(!SessionPolicy.HasEnoughCommit(25 * gib, false),
                      "Stanton refuses the headroom observed in the failed local run");
                string ini = "# comment\r\nmultiplayer = on\r\nmultiplayer = on\r\ncheck_updates = " +
                             "on\r\ngame = old\r\n";
                ini = SessionPolicy.SetIni(ini, "multiplayer", "off");
                Check(!ini.Contains("multiplayer = on"), "All duplicate safety settings are normalized");
                Check(ini.Contains("# comment"), "User comments are preserved");
                string literal = @"D:\Games\$SC\LIVE";
                ini = SessionPolicy.SetIni(ini, "game", literal);
                Check(ini.Contains("game = " + literal), "Dollar signs and backslashes stay literal");
                ini = SessionPolicy.SetIni(ini, "boot_map", "PU");
                Check(ini.Contains("boot_map = PU"), "Missing safety setting is appended");
                Reject(() => SessionPolicy.SetIni(ini, "game", "bad\r\nmultiplayer = on"),
                       "INI injection must be rejected");
                Check(SessionPolicy.IsOutOfMemory("Exception STATUS_CRYENGINE_OUT_OF_SYSMEM(0x2BADFF61)"),
                      "Recognizes observed game OOM");
                Check(!SessionPolicy.IsOutOfMemory("Is GPU crash: Yes"),
                      "Does not label GPU crashes as memory exhaustion");
                string root = Path.Combine(Path.GetTempPath(), "sc-offline-installer-fixture");
                Check(Paths.Below(Deployment.SafeEntry(root, "runtime/data/file.txt"), root),
                      "Valid package path");
                Reject(() => Deployment.SafeEntry(root, "../escape"), "Forward traversal");
                Reject(() => Deployment.SafeEntry(root, @"runtime\..\..\escape"), "Windows traversal");
                Reject(() => Deployment.SafeEntry(root, @"C:\escape"), "Absolute path");
                Reject(() => Deployment.SafeEntry(root, @"\\server\share"), "Network path");
                Reject(() => Deployment.SafeEntry(root, "runtime/file:stream"), "Alternate data stream");
                Reject(() => Paths.Session("../outside"), "Session journal traversal");
                string fixture = Path.Combine(
                    Path.GetTempPath(), "sc-offline-model-" + Guid.NewGuid().ToString("N"), "journal.json");
                Json.Write(fixture, new Settings { Game = literal, AllSystems = false });
                var settings = Json.Read<Settings>(fixture);
                Check(settings.Game == literal && !settings.AllSystems, "Atomic journal round trip");
                Json.Write(fixture, new Settings { Game = literal, AllSystems = true });
                Check(Json.Read<Settings>(fixture).AllSystems && !File.Exists(fixture + ".new"),
                      "Atomic journal replacement");
                File.Delete(fixture);
                Directory.Delete(Path.GetDirectoryName(fixture));
                string processRoot = Path.Combine(Path.GetTempPath(),
                                                  "sc-offline-reporter-test-" + Guid.NewGuid().ToString("N"));
                string fakeGame = Path.Combine(processRoot, "game");
                string fakeReporter = Path.Combine(fakeGame, "Tools", "Public", "CrashHandler.exe");
                string otherReporter = Path.Combine(processRoot, "unrelated", "CrashHandler.exe");
                Directory.CreateDirectory(Path.GetDirectoryName(fakeReporter));
                Directory.CreateDirectory(Path.GetDirectoryName(otherReporter));
                string self = System.Reflection.Assembly.GetExecutingAssembly().Location;
                File.Copy(self, fakeReporter);
                File.Copy(self, otherReporter);
                using (var owned = Process.Start(new ProcessStartInfo(fakeReporter, "--fixture-reporter") {
                    UseShellExecute = false, CreateNoWindow = true
                })) using (var unrelated =
                               Process.Start(new ProcessStartInfo(otherReporter, "--fixture-reporter") {
                                   UseShellExecute = false, CreateNoWindow = true
                               }))
                {
                    try
                    {
                        Thread.Sleep(200);
                        Check(Guard.CrashReporterRunning(fakeGame),
                              "Recognizes a reporter by its exact executable path");
                        Guard.StopCrashReporters(fakeGame);
                        Check(!Guard.CrashReporterRunning(fakeGame) && owned.WaitForExit(1000),
                              "Stops the session reporter before releasing protection");
                        Check(!unrelated.HasExited,
                              "Does not stop another application's same-named reporter");
                    }
                    finally
                    {
                        if (!owned.HasExited)
                        {
                            owned.Kill();
                            owned.WaitForExit();
                        }
                        if (!unrelated.HasExited)
                        {
                            unrelated.Kill();
                            unrelated.WaitForExit();
                        }
                    }
                }
                // All paths above were generated under this unique test root.
                File.Delete(fakeReporter);
                File.Delete(otherReporter);
                Directory.Delete(Path.GetDirectoryName(fakeReporter));
                Directory.Delete(Path.GetDirectoryName(Path.GetDirectoryName(fakeReporter)));
                Directory.Delete(fakeGame);
                Directory.Delete(Path.GetDirectoryName(otherReporter));
                Directory.Delete(processRoot);
                Console.WriteLine(
                    count + " desktop policy, memory, configuration, journal and installer checks passed.");
                return 0;
            }
            catch (Exception error)
            {
                Console.Error.WriteLine(error);
                return 1;
            }
        }
    }
}
