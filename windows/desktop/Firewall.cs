// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.ServiceProcess;

namespace ScOffline.Desktop
{
    // Windows adapters are kept out of the portable session policy tests.
    internal sealed class Firewall : IDisposable
    {
        public const string Group = "sc-offline desktop (session only)";
        private dynamic policy;
        public Firewall()
        {
            policy = Activator.CreateInstance(Type.GetTypeFromProgID("HNetCfg.FwPolicy2", true));
        }
        public void AssertEnabled()
        {
            using (var service = new ServiceController("MpsSvc")) if (
                service.Status != ServiceControllerStatus
                                      .Running) throw new InvalidOperationException("Windows Firewall is " +
                                                                                    "not running. The game " +
                                                                                    "was not started.");
            foreach (int profile in new[] { 1, 2, 4 })
                if (!(bool)policy.FirewallEnabled[profile])
                    throw new InvalidOperationException(
                        "Enable Windows Firewall for every profile before starting offline play.");
            if ((int)policy.LocalPolicyModifyState != 0)
                throw new InvalidOperationException(
                    "System policy prevents this launcher from managing effective firewall rules.");
        }
        public void Add(string id, string program, int direction)
        {
            dynamic rule = Activator.CreateInstance(Type.GetTypeFromProgID("HNetCfg.FWRule", true));
            try
            {
                rule.Name = id;
                rule.Description =
                    "Temporary offline session protection. Removed only after the game closes " +
                    "and the mod is cleaned up.";
                rule.Grouping = Group;
                rule.ApplicationName = program;
                rule.Protocol = 256;
                rule.Direction = direction;
                rule.Profiles = int.MaxValue;
                rule.Action = 0;
                rule.Enabled = true;
                rule.RemoteAddresses = "*";
                rule.LocalAddresses = "*";
                policy.Rules.Add(rule);
                dynamic applied = policy.Rules.Item(id);
                try
                {
                    if (!(bool)applied.Enabled || (int)applied.Action != 0 ||
                        (int)applied.Direction != direction || (int)applied.Protocol != 256 ||
                        (int)applied.Profiles != int.MaxValue || (string)applied.RemoteAddresses != "*" ||
                        !String.Equals((string)applied.ApplicationName, program,
                                       StringComparison.OrdinalIgnoreCase))
                        throw new InvalidOperationException(
                            "A firewall rule could not be verified. The game was not started.");
                }
                finally
                {
                    System.Runtime.InteropServices.Marshal.FinalReleaseComObject(applied);
                }
            }
            finally
            {
                System.Runtime.InteropServices.Marshal.FinalReleaseComObject(rule);
            }
        }
        public void Remove(string id)
        {
            try
            {
                dynamic rule = policy.Rules.Item(id);
                bool owned = (string)rule.Grouping == Group;
                System.Runtime.InteropServices.Marshal.FinalReleaseComObject(rule);
                if (!owned)
                    throw new InvalidOperationException(
                        "A recorded firewall rule belongs to another application.");
                policy.Rules.Remove(id);
            }
            catch (FileNotFoundException)
            {
                // Recovery may replay a journal after a rule was already deleted.
            }
            catch (System.Runtime.InteropServices.COMException e)
            {
                if ((uint)e.ErrorCode != 0x80070002)
                    throw;
            }
        }
        public void RemoveLegacy()
        {
            var names = new List<string>();
            foreach (dynamic rule in policy.Rules)
            {
                try
                {
                    if ((string)rule.Grouping == "SCOfflineAislado-SoloSesion")
                        names.Add((string)rule.Name);
                }
                finally
                {
                    System.Runtime.InteropServices.Marshal.FinalReleaseComObject(rule);
                }
            }
            foreach (string name in names)
                policy.Rules.Remove(name);
        }
        public void Dispose()
        {
            if (policy != null)
            {
                System.Runtime.InteropServices.Marshal.FinalReleaseComObject(policy);
                policy = null;
            }
        }
    }
}
