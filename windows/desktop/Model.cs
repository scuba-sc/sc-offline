// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace ScOffline.Desktop
{
    internal static class Json
    {
        public static T Read<T>(string path)
        {
            return new JavaScriptSerializer().Deserialize<T>(File.ReadAllText(path));
        }
        public static void Write(string path, object value)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temporary = path + ".new";
            using (var stream = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None))
            {
                byte[] data = Encoding.UTF8.GetBytes(new JavaScriptSerializer().Serialize(value));
                stream.Write(data, 0, data.Length);
                stream.Flush(true);
            }
            if (File.Exists(path))
                File.Replace(temporary, path, null);
            else
                File.Move(temporary, path);
        }
    }
    internal sealed class Settings
    {
        public string Game { get; set; }
        public bool AllSystems { get; set; }
    }
    internal sealed class SessionRecord
    {
        public string Id { get; set; }
        public string Game { get; set; }
        public int OwnerPid { get; set; }
        public long OwnerStarted { get; set; }
        public string Phase { get; set; }
        public List<string> Rules { get; set; }
        public bool HadEacBackup { get; set; }
    }
    internal static class Paths
    {
        public static readonly string Install = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        public static readonly string State = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "sc-offline-desktop");
        public static readonly string Backend = Path.Combine(Install, "runtime");
        public static readonly string Native = Path.Combine(Backend, "sc-offline.exe");
        public static readonly string Journal = Path.Combine(State, "session.json");
        public static readonly string Settings = Path.Combine(State, "settings.json");
        public static string Session(string id)
        {
            Guid parsed;
            if (!Guid.TryParseExact(id, "N", out parsed))
                throw new InvalidDataException("Invalid session identifier.");
            return Path.Combine(State, "sessions", id);
        }
        public static string Eac
        {
            get {
                return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                                    "EasyAntiCheat_EOS", "EasyAntiCheat_EOS.exe");
            }
        }
        public static string NativeJournal
        {
            get {
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "sc-offline",
                    "pc-changes.txt");
            }
        }
        public static string ValidateGame(string path)
        {
            string full = Path.GetFullPath(path).TrimEnd('\\');
            if (full.IndexOfAny(new[] { '"', '\r', '\n' }) >= 0 ||
                full.StartsWith(@"\\", StringComparison.Ordinal))
                throw new InvalidDataException("Choose a local game folder.");
            if (!File.Exists(Path.Combine(full, "Bin64", "StarCitizen.exe")) ||
                !File.Exists(Path.Combine(full, "Data.p4k")))
                throw new InvalidDataException(
                    "Choose the game channel folder containing Data.p4k and Bin64 " +
                    "(for example StarCitizen\\LIVE).");
            return full;
        }
        public static string Hash(string path)
        {
            using (var sha = SHA256.Create()) using (var file = File.OpenRead(path)) return BitConverter
                .ToString(sha.ComputeHash(file))
                .Replace("-", "")
                .ToLowerInvariant();
        }
        public static bool Below(string path, string root)
        {
            return Path.GetFullPath(path).StartsWith(Path.GetFullPath(root).TrimEnd('\\') + "\\",
                                                     StringComparison.OrdinalIgnoreCase);
        }
    }
    internal static class SessionPolicy
    {
        public static bool MayRelease(bool gameRunning, bool nativeRunning, bool leftovers,
                                      bool crashReporterRunning = false)
        {
            return !gameRunning && !nativeRunning && !leftovers && !crashReporterRunning;
        }
        public static void CompleteCleanup(Action restore, Func<bool> canRelease, Action release)
        {
            restore();
            if (!canRelease())
                throw new InvalidOperationException(
                    "Cleanup is incomplete. Session protection has been kept.");
            release();
        }
        public static bool IsOutOfMemory(string log)
        {
            return log.Contains("STATUS_CRYENGINE_OUT_OF_SYSMEM") ||
                   log.Contains("Is out of system memory: Yes");
        }
        public static string UserFailure(string log)
        {
            var match = Regex.Match(
                log,
                @"^.*(?:invalid[ _-]*user|user[ " +
                @"_-]*(?:information|info|session|profile)[^\r\n]{0,40}(?:invalid|not found)|(?:could " +
                @"not|couldn't|failed to)[^\r\n]{0,30}user[ _-]*(?:information|info))[^\r\n]*$",
                RegexOptions.IgnoreCase | RegexOptions.Multiline);
            return match.Success ? match.Value.Trim() : null;
        }
        public static bool LocalUserLoaded(string log)
        {
            return Regex.IsMatch(log, @"Player\[default_1\][^\r\n]*Result\[(?:succeed|success)\]",
                                 RegexOptions.IgnoreCase);
        }
        public static string SetIni(string text, string key, string value)
        {
            if (!Regex.IsMatch(key, "^[a-z_]+$") || value.IndexOfAny(new[] { '\r', '\n' }) >= 0)
                throw new InvalidDataException("Invalid setting.");
            var pattern = new Regex("^" + Regex.Escape(key) + @"\s*=.*$", RegexOptions.Multiline);
            string line = key + " = " + value;
            // A replacement callback keeps '$' and backslashes in game paths literal.
            return pattern.IsMatch(text) ? pattern.Replace(text, match => line)
                                         : text.TrimEnd() + "\r\n" + line + "\r\n";
        }
        public static void Configure(string game, bool allSystems)
        {
            string path = Path.Combine(Paths.Backend, "sc-offline.ini");
            string ini = File.ReadAllText(path);
            var settings = new Dictionary<string, string> { { "game", game },
                                                            { "channel", new DirectoryInfo(game).Name },
                                                            { "boot_map", allSystems ? "PU_All" : "PU" },
                                                            { "multiplayer", "off" },
                                                            { "multiplayer_allow", "" },
                                                            { "check_updates", "off" },
                                                            { "crash_reports", "off" },
                                                            { "clean_logs", "off" },
                                                            { "block_network", "on" },
                                                            { "eac_hosts", "on" },
                                                            { "eac_rename", "on" } };
            foreach (var entry in settings)
                ini = SetIni(ini, entry.Key, entry.Value);
            File.WriteAllText(path, ini, new UTF8Encoding(false));
            string discord = Path.Combine(Paths.Backend, "data", "plugins", "discord");
            if (Directory.Exists(discord))
                File.WriteAllText(Path.Combine(discord, "disabled"), "");
        }
    }
    internal static class Memory
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct Status
        {
            public uint Length, Load;
            public ulong TotalPhysical, AvailablePhysical, TotalPageFile, AvailablePageFile, TotalVirtual,
                AvailableVirtual, AvailableExtended;
        }
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GlobalMemoryStatusEx(ref Status status);
        public static ulong AvailableCommit()
        {
            var s = new Status();
            s.Length = (uint)Marshal.SizeOf(s);
            if (!GlobalMemoryStatusEx(ref s))
                throw new System.ComponentModel.Win32Exception();
            return s.AvailablePageFile;
        }
        public static string GiB(ulong bytes)
        {
            return (bytes / (1024.0 * 1024 * 1024)).ToString("0.0", CultureInfo.InvariantCulture) + " GB";
        }
    }
}
