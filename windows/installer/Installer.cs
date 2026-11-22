// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;
using ScOffline.Desktop;

namespace ScOffline.Setup
{
    internal static class Deployment
    {
        public const string Product = "sc-offline-desktop";
        public static readonly string Programs = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs");
        public static readonly string InstallRoot = Path.Combine(Programs, Product);
        public static readonly string Menu = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "SC Offline");
        private const string RegistryPath =
            @"Software\Microsoft\Windows\CurrentVersion\Uninstall\sc-offline-desktop";
        public static string Quote(string value)
        {
            return Program.Quote(value);
        }
        public static void AssertIdle()
        {
            if (Process.GetProcessesByName("SCOfflineLauncher").Length != 0 || Guard.GameRunning() ||
                Guard.NativeRunning() || File.Exists(Paths.Journal))
                throw new InvalidOperationException(
                    "Close the game and launcher first. Recover any interrupted offline session before " +
                    "installing or uninstalling.");
        }
        public static string SafeEntry(string root, string entry)
        {
            string normalized = entry.Replace('/', '\\');
            if (String.IsNullOrEmpty(normalized) || normalized.Split('\\').Any(part => part == "..") ||
                normalized.Contains(":") || Path.IsPathRooted(normalized))
                throw new InvalidDataException("Unsafe package path.");
            string full = Path.GetFullPath(Path.Combine(root, normalized));
            if (!Paths.Below(full, root))
                throw new InvalidDataException("Package entry is outside the installation.");
            return full;
        }
        public static void DeleteOwnedTree(string path)
        {
            string full = Path.GetFullPath(path).TrimEnd('\\');
            if (!Paths.Below(full, Programs) ||
                !Path.GetFileName(full).StartsWith(Product, StringComparison.Ordinal) ||
                !Directory.Exists(full))
                throw new InvalidOperationException("Invalid installation cleanup path.");
            CheckNoLinks(full);
            Directory.Delete(full, true);
        }
        private static void CheckNoLinks(string path)
        {
            if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Installation contains a link; cleanup stopped.");
            foreach (string item in Directory.GetFileSystemEntries(path))
            {
                if ((File.GetAttributes(item) & FileAttributes.ReparsePoint) != 0)
                    throw new IOException("Installation contains a link; cleanup stopped.");
                if (Directory.Exists(item))
                    CheckNoLinks(item);
            }
        }
        private static void PreserveData(string source, string destination)
        {
            if (!Directory.Exists(source))
                return;
            foreach (string relative in new[] { "sc-offline.ini", @"data\wallet.txt", @"data\spawn.txt",
                                                @"data\bookmarks.txt", @"data\locations_found.txt",
                                                @"data\launcher.log", @"data\mod.log", @"data\game-build.txt",
                                                @"data\game-path.txt" })
            {
                string file = Path.Combine(source, relative);
                if (File.Exists(file))
                {
                    string to = Path.Combine(destination, relative);
                    Directory.CreateDirectory(Path.GetDirectoryName(to));
                    File.Copy(file, to, true);
                }
            }
            string storage = Path.Combine(source, "data", "storage");
            if (Directory.Exists(storage))
            {
                CheckNoLinks(storage);
                foreach (string file in Directory.GetFiles(storage, "*", SearchOption.AllDirectories))
                {
                    string to =
                        Path.Combine(destination, "data", "storage", file.Substring(storage.Length + 1));
                    Directory.CreateDirectory(Path.GetDirectoryName(to));
                    File.Copy(file, to, true);
                }
            }
        }
        private static void Shortcut(string file, string target, string arguments = "")
        {
            Directory.CreateDirectory(Path.GetDirectoryName(file));
            dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell", true));
            dynamic shortcut = shell.CreateShortcut(file);
            try
            {
                shortcut.TargetPath = target;
                shortcut.Arguments = arguments;
                shortcut.WorkingDirectory = Path.GetDirectoryName(target);
                shortcut.IconLocation = Path.Combine(InstallRoot, "SCOfflineLauncher.exe") + ",0";
                shortcut.Save();
            }
            finally
            {
                System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shortcut);
                System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell);
            }
        }
        public static void Install(Action<string> progress)
        {
            AssertIdle();
            Directory.CreateDirectory(Programs);
            string stage = Path.Combine(Programs, Product + ".staging-" + Guid.NewGuid().ToString("N"));
            string rollback = InstallRoot + ".previous";
            if (Directory.Exists(rollback))
                throw new IOException("A previous installer rollback needs review: " + rollback);
            if (Directory.Exists(InstallRoot) &&
                (!File.Exists(Path.Combine(InstallRoot, "product.txt")) ||
                 File.ReadAllText(Path.Combine(InstallRoot, "product.txt")).Trim() != Product))
                throw new IOException("The installation folder belongs to another application.");
            bool switched = false;
            try
            {
                progress("Verifying the embedded offline runtime...");
                byte[] payload;
                string expected;
                using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(
                           "payload.zip")) using (var buffer = new MemoryStream())
                {
                    stream.CopyTo(buffer);
                    payload = buffer.ToArray();
                }
                using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(
                           "payload.sha256")) using (var reader = new StreamReader(stream)) expected =
                    reader.ReadToEnd().Trim();
                using (var hash = SHA256.Create()) if (
                    BitConverter.ToString(hash.ComputeHash(payload)).Replace("-", "").ToLowerInvariant() !=
                    expected) throw new InvalidDataException("The embedded package failed verification.");
                Directory.CreateDirectory(stage);
                using (var buffer = new MemoryStream(payload)) using (
                    var archive = new ZipArchive(buffer, ZipArchiveMode.Read))
                {
                    var entries = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                    foreach (var entry in archive.Entries)
                    {
                        string destination = SafeEntry(stage, entry.FullName);
                        if (!entries.Add(destination))
                            throw new InvalidDataException("Duplicate package entry.");
                        if (entry.FullName.EndsWith("/"))
                        {
                            Directory.CreateDirectory(destination);
                            continue;
                        }
                        Directory.CreateDirectory(Path.GetDirectoryName(destination));
                        using (var source = entry.Open()) using (
                            var output = new FileStream(destination, FileMode.CreateNew))
                            source.CopyTo(output);
                    }
                }
                if (!File.Exists(Path.Combine(stage, "SCOfflineLauncher.exe")) ||
                    !File.Exists(Path.Combine(stage, "runtime", "dinput8.dll")))
                    throw new InvalidDataException("The offline package is incomplete.");
                File.WriteAllText(Path.Combine(stage, "product.txt"), Product);
                PreserveData(Path.Combine(Paths.State, "saved-runtime"), Path.Combine(stage, "runtime"));
                if (Directory.Exists(InstallRoot))
                    PreserveData(Path.Combine(InstallRoot, "runtime"), Path.Combine(stage, "runtime"));
                File.Copy(Application.ExecutablePath, Path.Combine(stage, "SCOfflineUninstall.exe"));
                progress("Installing the launcher and registering SC Offline...");
                if (Directory.Exists(InstallRoot))
                    Directory.Move(InstallRoot, rollback);
                Directory.Move(stage, InstallRoot);
                switched = true;
                using (var key = Registry.CurrentUser.CreateSubKey(RegistryPath))
                {
                    key.SetValue("DisplayName", "SC Offline");
                    key.SetValue("DisplayVersion", "0.1.0 (runtime 0.7.7)");
                    key.SetValue("Publisher", "sc-offline contributors");
                    key.SetValue("InstallLocation", InstallRoot);
                    key.SetValue("DisplayIcon", Path.Combine(InstallRoot, "SCOfflineLauncher.exe"));
                    key.SetValue("UninstallString",
                                 Quote(Path.Combine(InstallRoot, "SCOfflineUninstall.exe")) +
                                     " --uninstall");
                    key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                    key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                    long bytes = Directory.GetFiles(InstallRoot, "*", SearchOption.AllDirectories)
                                     .Sum(file => new FileInfo(file).Length);
                    key.SetValue("EstimatedSize", (int)(bytes / 1024), RegistryValueKind.DWord);
                }
                Shortcut(Path.Combine(Menu, "SC Offline.lnk"),
                         Path.Combine(InstallRoot, "SCOfflineLauncher.exe"));
                Shortcut(Path.Combine(Menu, "Uninstall SC Offline.lnk"),
                         Path.Combine(InstallRoot, "SCOfflineUninstall.exe"), "--uninstall");
                // Replace the earlier script shortcut with the installed application.
                Shortcut(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                      "Star Citizen OFFLINE.lnk"),
                         Path.Combine(InstallRoot, "SCOfflineLauncher.exe"));
                Shortcut(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                      "Star Citizen RECUPERAR ONLINE.lnk"),
                         Path.Combine(InstallRoot, "SCOfflineLauncher.exe"), "--restore");
                Shortcut(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                      "Star Citizen OFFLINE - instrucciones.lnk"),
                         Path.Combine(InstallRoot, "README.txt"));
                if (Directory.Exists(rollback))
                    DeleteOwnedTree(rollback);
                progress("Installed. Open SC Offline from the desktop, Start menu, or Windows Apps.");
            }
            catch
            {
                if (Directory.Exists(stage))
                    DeleteOwnedTree(stage);
                if (Directory.Exists(rollback))
                {
                    if (switched && Directory.Exists(InstallRoot))
                        DeleteOwnedTree(InstallRoot);
                    if (!Directory.Exists(InstallRoot))
                        Directory.Move(rollback, InstallRoot);
                }
                throw;
            }
        }
        public static void Uninstall()
        {
            AssertIdle();
            if (!File.Exists(Path.Combine(InstallRoot, "product.txt")) ||
                File.ReadAllText(Path.Combine(InstallRoot, "product.txt")).Trim() != Product)
                throw new IOException("SC Offline installation was not found.");
            PreserveData(Path.Combine(InstallRoot, "runtime"), Path.Combine(Paths.State, "saved-runtime"));
            DeleteOwnedTree(InstallRoot);
            Registry.CurrentUser.DeleteSubKeyTree(RegistryPath, false);
            foreach (string file in new[] {
                         Path.Combine(Menu, "SC Offline.lnk"), Path.Combine(Menu, "Uninstall SC Offline.lnk"),
                         Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                      "Star Citizen OFFLINE.lnk"),
                         Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                      "Star Citizen RECUPERAR ONLINE.lnk"),
                         Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                      "Star Citizen OFFLINE - instrucciones.lnk")
                     })
            {
                if (!File.Exists(file))
                    continue;
                dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell", true));
                dynamic shortcut = shell.CreateShortcut(file);
                bool owned;
                try
                {
                    owned = Paths.Below((string)shortcut.TargetPath, InstallRoot);
                }
                finally
                {
                    System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shortcut);
                    System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell);
                }
                if (owned)
                    File.Delete(file);
            }
            if (Directory.Exists(Menu) && Directory.GetFileSystemEntries(Menu).Length == 0)
                Directory.Delete(Menu);
        }
    }
    internal sealed class InstallerForm : Form
    {
        private readonly Button install = new Button();
        private readonly Button launch = new Button();
        private readonly Label status = new Label();
        public InstallerForm()
        {
            Text = "Install SC Offline";
            Size = new Size(660, 430);
            MinimumSize = Size;
            MaximumSize = Size;
            StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Segoe UI", 10);
            BackColor = Color.FromArgb(15, 22, 34);
            ForeColor = Color.White;
            Controls.Add(new Label { Text = "SC OFFLINE", Font = new Font("Segoe UI", 26, FontStyle.Bold),
                                     Location = new Point(30, 25), Size = new Size(590, 55) });
            Controls.Add(new Label {
                Text = "A desktop launcher for offline Star Citizen sessions.\r\nNo game files are " +
                       "included or downloaded.\r\n\r\nInstalls for your Windows account, with Start " +
                       "menu, desktop\r\nand Windows Apps entries. No scripts to run when playing.",
                Location = new Point(30, 95), Size = new Size(590, 125)
            });
            status.Text = "Runtime 0.7.7. Offline use still carries account risk.";
            status.Location = new Point(30, 230);
            status.Size = new Size(590, 60);
            Controls.Add(status);
            install.Text = "Install";
            install.Location = new Point(30, 310);
            install.Size = new Size(180, 45);
            Controls.Add(install);
            launch.Text = "Open SC Offline";
            launch.Location = new Point(230, 310);
            launch.Size = new Size(180, 45);
            launch.Enabled = false;
            Controls.Add(launch);
            install.Click += async (s, e) =>
            {
                install.Enabled = false;
                try
                {
                    await Task.Run(() => Deployment.Install(
                                       message => BeginInvoke(new Action(() => status.Text = message))));
                    launch.Enabled = true;
                }
                catch (Exception error)
                {
                    status.Text = error.Message;
                    install.Enabled = true;
                }
            };
            launch.Click += (s, e) =>
            {
                Process.Start(Path.Combine(Deployment.InstallRoot, "SCOfflineLauncher.exe"));
                Close();
            };
            FormClosing += (s, e) =>
            {
                if (!install.Enabled && !launch.Enabled)
                    e.Cancel = true;
            };
        }
    }
    internal static class SetupProgram
    {
        [STAThread]
        public static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try
            {
                if (args.Length == 1 && args[0] == "--install-current-user")
                {
                    Deployment.Install(message =>
                                       {});
                    return 0;
                }
                if (args.Length == 1 && args[0] == "--uninstall-current-user")
                {
                    Deployment.Uninstall();
                    return 0;
                }
                if (args.Length == 1 && args[0] == "--uninstall")
                {
                    if (MessageBox.Show(
                            "Uninstall SC Offline? Saves and settings will be kept. Close the game " +
                                "and recover any interrupted session first.",
                            "SC Offline", MessageBoxButtons.OKCancel) != DialogResult.OK)
                        return 0;
                    Deployment.AssertIdle();
                    string temp = Path.Combine(Path.GetTempPath(),
                                               "SCOfflineUninstall-" + Guid.NewGuid().ToString("N") + ".exe");
                    File.Copy(Application.ExecutablePath, temp);
                    Process.Start(temp, "--remove " + Process.GetCurrentProcess().Id);
                    return 0;
                }
                if (args.Length == 2 && args[0] == "--remove")
                {
                    int pid;
                    if (!Int32.TryParse(args[1], out pid))
                        throw new ArgumentException("Invalid uninstall request.");
                    try
                    {
                        using (var parent = Process.GetProcessById(pid)) parent.WaitForExit(30000);
                    }
                    catch (ArgumentException)
                    {
                    }
                    Deployment.Uninstall();
                    MessageBox.Show("SC Offline was uninstalled. Saves and settings were kept.",
                                    "SC Offline");
                    return 0;
                }
                if (args.Length != 0)
                    throw new ArgumentException("Unknown installer command.");
                Application.Run(new InstallerForm());
                return 0;
            }
            catch (Exception error)
            {
                Directory.CreateDirectory(Paths.State);
                File.WriteAllText(Path.Combine(Paths.State, "installer-error.txt"), error.ToString());
                if (args.Length == 0 || args[0] != "--install-current-user")
                    MessageBox.Show(error.Message, "SC Offline setup", MessageBoxButtons.OK,
                                    MessageBoxIcon.Error);
                return 1;
            }
        }
    }
}
