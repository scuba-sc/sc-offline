import unittest

from core_candidate_pr_guard import accepted_state


HEAD = "4c09ab2f73caa3d1103214adc56193d2434a8795"
OTHER = "f76eed986135a94f7283efb29ee412979a60275a"


def open_pr(head=HEAD):
    return {
        "state": "open",
        "head": {"sha": head},
        "base": {"ref": "main"},
        "merged": False,
        "merged_at": None,
        "merge_commit_sha": None,
    }


def merged_pr(head=HEAD):
    return {
        "state": "closed",
        "head": {"sha": head},
        "base": {"ref": "main"},
        "merged": True,
        "merged_at": "2026-11-01T00:00:00Z",
        "merge_commit_sha": "1" * 40,
    }


class CandidatePrGuardTests(unittest.TestCase):
    def test_accepts_exact_open_head(self):
        self.assertEqual(accepted_state(open_pr(), HEAD), "open")

    def test_accepts_exact_genuinely_merged_head(self):
        self.assertEqual(accepted_state(merged_pr(), HEAD), "merged")

    def test_rejects_stale_open_head(self):
        self.assertIsNone(accepted_state(open_pr(OTHER), HEAD))

    def test_rejects_closed_unmerged_pr(self):
        abandoned = open_pr()
        abandoned.update(state="closed", merged=False)
        self.assertIsNone(accepted_state(abandoned, HEAD))

    def test_rejects_merged_pr_with_different_reviewed_head(self):
        self.assertIsNone(accepted_state(merged_pr(OTHER), HEAD))

    def test_rejects_merged_pr_to_a_different_base(self):
        wrong_base = merged_pr()
        wrong_base["base"]["ref"] = "release"
        self.assertIsNone(accepted_state(wrong_base, HEAD))

    def test_rejects_merged_state_without_merge_receipt(self):
        incomplete = merged_pr()
        incomplete["merge_commit_sha"] = None
        self.assertIsNone(accepted_state(incomplete, HEAD))

    def test_rejects_merged_state_without_timestamp(self):
        incomplete = merged_pr()
        incomplete["merged_at"] = None
        self.assertIsNone(accepted_state(incomplete, HEAD))

    def test_rejects_malformed_merge_timestamp(self):
        malformed = merged_pr()
        malformed["merged_at"] = "bogus"
        self.assertIsNone(accepted_state(malformed, HEAD))

    def test_rejects_invalid_calendar_merge_timestamp(self):
        malformed = merged_pr()
        malformed["merged_at"] = "2026-13-99T00:00:00Z"
        self.assertIsNone(accepted_state(malformed, HEAD))

    def test_rejects_malformed_merge_commit_sha(self):
        malformed = merged_pr()
        malformed["merge_commit_sha"] = "not-a-sha"
        self.assertIsNone(accepted_state(malformed, HEAD))

    def test_rejects_open_pr_claiming_merged(self):
        inconsistent = merged_pr()
        inconsistent["state"] = "open"
        self.assertIsNone(accepted_state(inconsistent, HEAD))

    def test_rejects_unknown_pr_state(self):
        unknown = open_pr()
        unknown["state"] = "locked"
        self.assertIsNone(accepted_state(unknown, HEAD))


if __name__ == "__main__":
    unittest.main()
