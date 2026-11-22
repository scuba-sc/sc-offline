// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Windows.Forms;

namespace ScOffline.Desktop
{
    internal static class Program
    {
        public static string Quote(string value)
        {
            if (value.IndexOf('"') >= 0)
                throw new ArgumentException("Invalid path.");
            return "\"" + value.TrimEnd('\\') + "\"";
        }
        private static readonly object logLock = new object();
        public static void Log(string text)
        {
            lock (logLock)
            {
                Directory.CreateDirectory(Paths.State);
                File.AppendAllText(Path.Combine(Paths.State, "desktop.log"),
                                   DateTime.UtcNow.ToString("o") + " " + text + Environment.NewLine);
            }
        }
        public static Process Elevate(string arguments)
        {
            return Process.Start(new ProcessStartInfo(Application.ExecutablePath,
                                                      arguments) { UseShellExecute = true, Verb = "runas",
                                                                   WindowStyle = ProcessWindowStyle.Hidden,
                                                                   WorkingDirectory = Paths.Install });
        }
        private static int ExclusiveGuard(Func<int> operation, bool recovery)
        {
            using (var mutex = new Mutex(false, @"Global\sc-offline-desktop-session-guard"))
            {
                bool acquired;
                try
                {
                    acquired = mutex.WaitOne(recovery ? 10000 : 0);
                }
                catch (AbandonedMutexException)
                {
                    acquired = true;
                }
                if (!acquired)
                    throw new InvalidOperationException("Another offline session supervisor is active. " +
                                                        "Close its game and allow cleanup to finish.");
                try
                {
                    return operation();
                }
                finally
                {
                    mutex.ReleaseMutex();
                }
            }
        }
        public static void VerifyRuntime()
        {
            var expected =
                Json.Read<Dictionary<string, string>>(Path.Combine(Paths.Install, "runtime-hashes.json"));
            if (!expected.ContainsKey("sc-offline.exe") || !expected.ContainsKey("dinput8.dll"))
                throw new InvalidDataException(
                    "The installed runtime manifest is incomplete. Reinstall the application.");
            foreach (var entry in expected)
            {
                string file = Path.GetFullPath(Path.Combine(Paths.Backend, entry.Key));
                if (!Paths.Below(file, Paths.Backend) || !File.Exists(file) ||
                    Paths.Hash(file) != entry.Value)
                    throw new InvalidDataException(
                        "A runtime file is missing or changed. Reinstall the application: " + entry.Key);
            }
        }
        private static int Probe(string address)
        {
            try
            {
                using (var socket = new TcpClient())
                {
                    var task = socket.ConnectAsync(IPAddress.Parse(address), 443);
                    if (!task.Wait(4000))
                        return 2;
                    return socket.Connected ? 0 : 3;
                }
            }
            catch
            {
                return 4;
            }
        }
        private static int ChildProbe(string address)
        {
            using (var process = Process.Start(new ProcessStartInfo(
                       Application.ExecutablePath, "--network-probe " + address) { UseShellExecute = false,
                                                                                   CreateNoWindow = true }))
            {
                if (!process.WaitForExit(10000))
                {
                    process.Kill();
                    return 5;
                }
                return process.ExitCode;
            }
        }
        private static int TestFirewall()
        {
            string address = Dns.GetHostAddresses("api.github.com")
                                 .First(a => a.AddressFamily == AddressFamily.InterNetwork)
                                 .ToString();
            string id = "sc-offline-desktop-probe-" + Guid.NewGuid().ToString("N");
            using (var firewall = new Firewall())
            {
                firewall.AssertEnabled();
                if (ChildProbe(address) != 0)
                    throw new InvalidOperationException("The unblocked reference connection failed.");
                try
                {
                    firewall.Add(id + "-in", Application.ExecutablePath, 1);
                    firewall.Add(id + "-out", Application.ExecutablePath, 2);
                    if (ChildProbe(address) == 0)
                        throw new InvalidOperationException(
                            "The firewall did not block the test executable.");
                }
                finally
                {
                    firewall.Remove(id + "-in");
                    firewall.Remove(id + "-out");
                }
                if (ChildProbe(address) != 0)
                    throw new InvalidOperationException("Connectivity was not restored after the test.");
            }
            Json.Write(Path.Combine(Paths.State, "firewall-test.json"),
                       new { passed = true, reference = "api.github.com:443", before = "connected",
                             during = "blocked", after = "connected", utc = DateTime.UtcNow.ToString("o") });
            return 0;
        }
        [STAThread]
        public static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try
            {
                if (args.Length == 2 && args[0] == "--guard")
                    return ExclusiveGuard(() => Guard.Session(args[1]), false);
                if (args.Length == 1 && args[0] == "--recover")
                    return ExclusiveGuard(() => Guard.Recover(), true);
                if (args.Length == 2 && args[0] == "--network-probe")
                    return Probe(args[1]);
                if (args.Length == 1 && args[0] == "--test-firewall")
                    return TestFirewall();
                bool restoreOnOpen = args.Length == 1 && args[0] == "--restore";
                if (args.Length != 0 && !restoreOnOpen)
                    throw new ArgumentException("Unknown launcher command.");
                bool created;
                using (var mutex = new Mutex(true, @"Local\sc-offline-desktop-ui", out created))
                {
                    if (!created)
                    {
                        MessageBox.Show("SC Offline is already open. Switch to its launcher window.",
                                        "SC Offline");
                        return 1;
                    }
                    Directory.CreateDirectory(Paths.State);
                    Application.Run(new LauncherForm(restoreOnOpen));
                }
                return 0;
            }
            catch (Exception error)
            {
                Log(error.ToString());
                // Background helpers must never leave an invisible error dialog.
                if (args.Length == 0)
                    MessageBox.Show(error.Message, "SC Offline", MessageBoxButtons.OK, MessageBoxIcon.Error);
                else
                    File.WriteAllText(Path.Combine(Paths.State, "helper-error.txt"), error.Message);
                return 1;
            }
        }
    }
}
