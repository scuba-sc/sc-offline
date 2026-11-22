// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace ScOffline.Desktop
{
    internal sealed class LauncherForm : Form
    {
        private readonly TextBox gamePath = new TextBox();
        private readonly TextBox log = new TextBox();
        private readonly Label status = new Label();
        private readonly Label memory = new Label();
        private readonly CheckBox allSystems = new CheckBox();
        private readonly Button play = new Button();
        private readonly Button recover = new Button();
        private readonly Button stop = new Button();
        private readonly Button browse = new Button();
        private bool busy, closeWhenDone;
        private string activeSession;
        private readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
        public LauncherForm(bool restoreOnOpen = false)
        {
            Text = "SC Offline";
            MinimumSize = new Size(860, 650);
            Size = new Size(960, 740);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Color.FromArgb(15, 22, 34);
            ForeColor = Color.FromArgb(229, 237, 249);
            Font = new Font("Segoe UI", 10);
            Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(28),
                                                ColumnCount = 1, RowCount = 9 };
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 46));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 60));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 36));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            Controls.Add(layout);
            layout.Controls.Add(new Label { Text = "SC OFFLINE",
                                            Font = new Font("Segoe UI", 26, FontStyle.Bold),
                                            Dock = DockStyle.Fill },
                                0, 0);
            layout.Controls.Add(
                new Label { Text = "Offline sessions with automatic cleanup. Your other apps stay online.",
                            Dock = DockStyle.Fill, ForeColor = Color.LightSteelBlue },
                0, 1);
            layout.Controls.Add(new Label { Text = "Star Citizen game folder", Dock = DockStyle.Fill }, 0, 2);
            var pathPanel = new TableLayoutPanel { ColumnCount = 2, Dock = DockStyle.Fill };
            pathPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            pathPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 110));
            gamePath.Dock = DockStyle.Fill;
            StyleText(gamePath);
            pathPanel.Controls.Add(gamePath, 0, 0);
            StyleButton(browse, "Browse...");
            browse.Click += (s, e) =>
            {
                using (var dialog = new FolderBrowserDialog {
                    Description = "Select the channel folder containing Data.p4k and Bin64",
                    SelectedPath = gamePath.Text
                }) if (dialog.ShowDialog(this) == DialogResult.OK) gamePath.Text = dialog.SelectedPath;
            };
            pathPanel.Controls.Add(browse, 1, 0);
            layout.Controls.Add(pathPanel, 0, 3);
            allSystems.Text = "Load every star system (experimental; uses more memory)";
            allSystems.Dock = DockStyle.Fill;
            layout.Controls.Add(allSystems, 0, 4);
            memory.Dock = DockStyle.Fill;
            memory.ForeColor = Color.LightSteelBlue;
            layout.Controls.Add(memory, 0, 5);
            var actions = new FlowLayoutPanel { Dock = DockStyle.Fill };
            StyleButton(play, "Play offline");
            play.BackColor = Color.FromArgb(27, 112, 159);
            play.Width = 160;
            play.Click += async (s, e) => await StartSession();
            actions.Controls.Add(play);
            StyleButton(stop, "Close game");
            stop.Enabled = false;
            stop.Click += async (s, e) => await Task.Run(() => Guard.StopGame());
            actions.Controls.Add(stop);
            StyleButton(recover, "Restore online");
            recover.Click += async (s, e) => await Recover();
            actions.Controls.Add(recover);
            var logs = new Button();
            StyleButton(logs, "Open logs");
            logs.Click += (s, e) => Process.Start("explorer.exe", Program.Quote(Paths.State));
            actions.Controls.Add(logs);
            var paging = new Button();
            StyleButton(paging, "Virtual memory");
            paging.Width = 140;
            paging.Click += async (s, e) => await SetMemory();
            actions.Controls.Add(paging);
            layout.Controls.Add(actions, 0, 6);
            status.Text = "Ready. No session firewall rules are active.";
            status.Dock = DockStyle.Fill;
            layout.Controls.Add(status, 0, 7);
            log.Multiline = true;
            log.ReadOnly = true;
            log.ScrollBars = ScrollBars.Vertical;
            log.Dock = DockStyle.Fill;
            log.Font = new Font("Consolas", 9);
            StyleText(log);
            layout.Controls.Add(log, 0, 8);
            var settings = File.Exists(Paths.Settings) ? Json.Read<Settings>(Paths.Settings) : new Settings();
            gamePath.Text = settings.Game ??
                            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                                         "Roberts Space Industries", "StarCitizen", "LIVE");
            allSystems.Checked = settings.AllSystems;
            if (File.Exists(Paths.Journal))
            {
                status.Text = "An interrupted session needs recovery. Use Restore online.";
                Append("Protection may remain from an interrupted session. Restore online cleans the mod " +
                       "before removing it.");
            }
            Append("SC Offline runtime 0.7.7. Default start: Stanton.");
            Append("Offline use can still carry account risk. Network isolation is not an account-safety " +
                   "guarantee.");
            timer.Interval = 2000;
            timer.Tick += (s, e) => UpdateMemory();
            timer.Start();
            UpdateMemory();
            FormClosing += OnClosing;
            if (restoreOnOpen)
                Shown += async (s, e) => await Recover();
        }
        private static void StyleText(TextBox box)
        {
            box.BackColor = Color.FromArgb(23, 34, 49);
            box.ForeColor = Color.FromArgb(229, 237, 249);
            box.BorderStyle = BorderStyle.FixedSingle;
        }
        private static void StyleButton(Button button, string text)
        {
            button.Text = text;
            button.Width = 132;
            button.Height = 40;
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderColor = Color.FromArgb(59, 80, 107);
            button.BackColor = Color.FromArgb(31, 47, 66);
            button.ForeColor = Color.White;
            button.Margin = new Padding(0, 0, 8, 0);
        }
        private void Append(string line)
        {
            if (IsDisposed)
                return;
            if (InvokeRequired)
            {
                BeginInvoke(new Action<string>(Append), line);
                return;
            }
            log.AppendText(line + Environment.NewLine);
        }
        private void SetStatus(string text)
        {
            if (IsDisposed)
                return;
            if (InvokeRequired)
            {
                BeginInvoke(new Action<string>(SetStatus), text);
                return;
            }
            status.Text = text;
        }
        private void UpdateMemory()
        {
            try
            {
                memory.Text = "Available committed memory: " + Memory.GiB(Memory.AvailableCommit()) +
                              ". Default start: Stanton only.";
            }
            catch
            {
                memory.Text = "Memory information unavailable.";
            }
        }
        private void Busy(bool value)
        {
            busy = value;
            play.Enabled = !value;
            recover.Enabled = !value;
            browse.Enabled = !value;
            gamePath.Enabled = !value;
            allSystems.Enabled = !value;
            stop.Enabled = value;
        }
        private void OnClosing(object sender, FormClosingEventArgs e)
        {
            if (!busy)
                return;
            e.Cancel = true;
            if (closeWhenDone)
                return;
            if (MessageBox.Show(this, "Close the game and exit after restoring the offline changes?",
                                "SC Offline", MessageBoxButtons.YesNo,
                                MessageBoxIcon.Question) == DialogResult.Yes)
            {
                closeWhenDone = true;
                SetStatus("Waiting for the game to close and cleanup to finish...");
                Task.Run(() => Guard.StopGame());
                if (activeSession != null)
                    File.WriteAllText(Path.Combine(Paths.Session(activeSession), "cancel"), "cancel");
            }
        }
        private async Task SetMemory()
        {
            if (busy)
                return;
            if (MessageBox.Show(
                    this,
                    "Configure a 32-48 GB Windows paging file on the system drive? This is a persistent " +
                        "Windows setting and requires a restart. Other paging drives are preserved.",
                    "Virtual memory", MessageBoxButtons.OKCancel,
                    MessageBoxIcon.Information) != DialogResult.OK)
                return;
            try
            {
                using (var p = Program.Elevate("--configure-memory"))
                {
                    await Task.Run(() => p.WaitForExit());
                    if (p.ExitCode != 0)
                        throw new InvalidOperationException(
                            "Could not configure virtual memory. See helper-error.txt.");
                }
                MessageBox.Show(this,
                                "Restart Windows to activate the larger paging file. No restart was " +
                                    "performed automatically.",
                                "Virtual memory");
            }
            catch (Exception error)
            {
                MessageBox.Show(this, error.Message, "Virtual memory");
            }
        }
        private async Task Recover()
        {
            Busy(true);
            try
            {
                string game = Paths.ValidateGame(gamePath.Text);
                Json.Write(Paths.Settings, new Settings { Game = game, AllSystems = allSystems.Checked });
                using (var process = Program.Elevate("--recover"))
                {
                    await Task.Run(() => process.WaitForExit());
                    if (process.ExitCode != 0)
                        throw new InvalidOperationException(
                            "Recovery could not complete. " +
                            (File.Exists(Path.Combine(Paths.State, "helper-error.txt"))
                                 ? File.ReadAllText(Path.Combine(Paths.State, "helper-error.txt"))
                                 : "See desktop.log."));
                }
                SetStatus("Game restored. Temporary firewall rules removed.");
                Append("Recovery complete. You can open RSI Launcher normally.");
            }
            catch (Exception error)
            {
                SetStatus("Recovery needs attention.");
                Append(error.Message);
            }
            finally
            {
                Busy(false);
            }
        }
        private async Task StartSession()
        {
            DateTime sessionStarted = DateTime.UtcNow;
            string id = null;
            Busy(true);
            try
            {
                string game = Paths.ValidateGame(gamePath.Text);
                bool all = allSystems.Checked;
                if (Guard.GameRunning() || Guard.NativeRunning())
                    throw new InvalidOperationException(
                        "Close Star Citizen and any other sc-offline window first.");
                if (File.Exists(Paths.Journal) || Guard.Leftovers(game))
                    throw new InvalidOperationException("Use Restore online before starting a new session.");
                Program.VerifyRuntime();
                ulong available = Memory.AvailableCommit();
                if (!SessionPolicy.HasEnoughCommit(available, all))
                    throw new InvalidOperationException(
                        "Only " + Memory.GiB(available) +
                        " of committed memory is available. This start needs at least " +
                        Memory.GiB(SessionPolicy.MinimumCommit(all)) +
                        (". Close memory-heavy apps, select Stanton only, or increase virtual memory and " +
                         "restart Windows."));
                Json.Write(Paths.Settings, new Settings { Game = game, AllSystems = all });
                SessionPolicy.Configure(game, all);
                id = Guid.NewGuid().ToString("N");
                Directory.CreateDirectory(Paths.Session(id));
                activeSession = id;
                using (var owner = Process.GetCurrentProcess()) Json.Write(
                    Paths.Journal, new SessionRecord { Id = id, Game = game, OwnerPid = owner.Id,
                                                       OwnerStarted = owner.StartTime.ToUniversalTime().Ticks,
                                                       Phase = "Requested", Rules = new List<string>() });
                SetStatus("Preparing temporary isolation. Approve the Windows administrator prompt.");
                using (var guard = Program.Elevate("--guard " + id))
                {
                    string session = Paths.Session(id);
                    await Task.Run(() =>
                                   {
                                       while (!File.Exists(Path.Combine(session, "ready")))
                                       {
                                           if (File.Exists(Path.Combine(session, "error")))
                                               throw new InvalidOperationException(
                                                   File.ReadAllText(Path.Combine(session, "error")));
                                           if (guard.HasExited)
                                               throw new InvalidOperationException(
                                                   "The protection helper could not start. See desktop.log.");
                                           Thread.Sleep(250);
                                       }
                                   });
                    SetStatus("Offline protection verified. Starting the game...");
                    Append(
                        "Session firewall active for the game, RSI, crash reporter and runtime. Other apps " +
                        "stay online.");
                    int nativeCode;
                    try
                    {
                        nativeCode =
                            closeWhenDone ? 0 : await Task.Run(() => Guard.RunNative("play", game, Append));
                    }
                    finally
                    {
                        File.WriteAllText(Path.Combine(session, "done"), "done");
                    }
                    SetStatus("Game closed. Restoring temporary changes...");
                    await Task.Run(() =>
                                   {
                                       while (!File.Exists(Path.Combine(session, "finished")))
                                       {
                                           if (File.Exists(Path.Combine(session, "error")))
                                               throw new InvalidOperationException(
                                                   File.ReadAllText(Path.Combine(session, "error")));
                                           if (guard.HasExited)
                                               throw new InvalidOperationException(
                                                   "Cleanup was not confirmed. Use Restore online.");
                                           Thread.Sleep(250);
                                       }
                                   });
                    string gameLog = Path.Combine(game, "Game.log");
                    if (File.Exists(gameLog) && File.GetLastWriteTimeUtc(gameLog) >= sessionStarted &&
                        SessionPolicy.IsOutOfMemory(File.ReadAllText(gameLog)))
                    {
                        SetStatus("Game ran out of committed memory. Cleanup completed.");
                        Append("The game log reports system memory exhaustion. Use Virtual memory, restart " +
                               "Windows, and keep Stanton only selected.");
                    }
                    else
                    {
                        SetStatus("Session ended. Game restored; no temporary network blocks remain.");
                        Append("Session cleanup confirmed. Native runtime exit code: " + nativeCode);
                    }
                }
            }
            catch (Exception error)
            {
                Program.Log(error.ToString());
                Append(error.Message);
                SetStatus("Could not complete the offline session. See details below.");
            }
            finally
            {
                if (id != null)
                {
                    File.WriteAllText(Path.Combine(Paths.Session(id), "done"), "done");
                    // If elevation was cancelled before any helper ran, no mutation took place.
                    if (File.Exists(Paths.Journal))
                    {
                        var session = Json.Read<SessionRecord>(Paths.Journal);
                        if (session.Id == id && session.Phase == "Requested" && session.Rules.Count == 0)
                            File.Delete(Paths.Journal);
                    }
                }
                activeSession = null;
                Busy(false);
                if (closeWhenDone)
                    Close();
            }
        }
    }
}
