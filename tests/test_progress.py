"""Acceptance-ledger tests; every mutable fixture lives in a temporary directory."""

from __future__ import annotations

import contextlib
import copy
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sekiro_progress", REPOSITORY / "tools" / "progress.py")
progress = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(progress)
BASELINE = json.loads((REPOSITORY / "progress.json").read_text(encoding="utf-8"))


class ProgressTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.ledger = copy.deepcopy(BASELINE)
        # Tests are independent of the accepted completion state of the real project.
        for group in self.ledger["groups"]:
            for criterion in group["criteria"]:
                criterion["status"] = "planned"
                criterion["evidence"] = None
        self.readme = "# Fixture\n\nText before.\n" + progress.START + "\n" + progress.END + "\nText after.\n"
        (self.root / "README.md").write_text(self.readme, encoding="utf-8")

    def first(self):
        return self.ledger["groups"][0]["criteria"][0]

    def valid_evidence(self, criterion=None):
        criterion = criterion or self.first()
        report = self.root / "reports" / "manual-test.md"
        report.parent.mkdir(exist_ok=True)
        report.write_text("# Synthetic test fixture\n\nSteps and result for the test only.\n", encoding="utf-8")
        return {
            "commit": "a" * 40,
            "game_executable_sha256": "b" * 64,
            "game_version": "fixture-1.06",
            "scenario": "Synthetic fixture, not a real game test.",
            "tested_on": "2026-10-20",
            "tested_by": "Unit test fixture",
            "participants": {
                "players": criterion["min_players"],
                "machines": criterion["min_machines"],
            },
            "result": "passed",
            "report": "reports/manual-test.md",
        }

    def accept(self, criterion=None):
        criterion = criterion or self.first()
        criterion["status"] = "verified"
        criterion["evidence"] = self.valid_evidence(criterion)
        return criterion

    def write_ledger(self):
        (self.root / "progress.json").write_text(json.dumps(self.ledger), encoding="utf-8")

    def run_cli(self, *args):
        self.write_ledger()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return progress.main(["--root", str(self.root), *args])

    def snapshot(self):
        return {str(path.relative_to(self.root)): path.read_bytes()
                for path in self.root.rglob("*") if path.is_file()}

    def test_planned_baseline_is_zero_and_weights_are_100(self):
        progress.validate(self.ledger, self.root)
        self.assertEqual(0, progress.percent(self.ledger))
        self.assertEqual(100, sum(group["weight"] for group in self.ledger["groups"]))

    def test_only_verified_atomic_criteria_count(self):
        criterion = self.accept()
        other = self.ledger["groups"][1]["criteria"][0]
        other["status"] = "in_progress"
        other["evidence"] = self.valid_evidence(other)
        progress.validate(self.ledger, self.root)
        self.assertEqual(criterion["weight"], progress.percent(self.ledger))

    def test_preparation_and_closed_or_cancelled_issues_never_add_points(self):
        for item in self.ledger["preparation"]:
            item["status"] = "ready"
        self.first()["issue_state"] = "closed"
        self.first()["issue_reason"] = "duplicate"
        self.ledger["groups"][1]["criteria"][0]["issue_reason"] = "not_planned"
        progress.validate(self.ledger, self.root)
        self.assertEqual(0, progress.percent(self.ledger))

    def test_regression_removes_accepted_weight_even_if_old_evidence_remains(self):
        criterion = self.accept()
        self.assertEqual(criterion["weight"], progress.percent(self.ledger))
        criterion["status"] = "blocked"
        progress.validate(self.ledger, self.root)
        self.assertEqual(0, progress.percent(self.ledger))

    def test_every_criterion_can_be_verified_to_exactly_100(self):
        for group in self.ledger["groups"]:
            for criterion in group["criteria"]:
                self.accept(criterion)
        progress.validate(self.ledger, self.root)
        self.assertEqual(100, progress.percent(self.ledger))

    def test_duplicate_ids_fail_including_across_preparation(self):
        self.ledger["preparation"][0]["id"] = self.first()["id"]
        with self.assertRaisesRegex(progress.ProgressError, "duplicate ID"):
            progress.validate(self.ledger, self.root)

    def test_weight_or_group_baseline_changes_fail(self):
        self.first()["weight"] += 1
        with self.assertRaisesRegex(progress.ProgressError, "baseline values must remain"):
            progress.validate(self.ledger, self.root)
        self.first()["weight"] -= 1
        self.ledger["groups"][0]["weight"] = 11
        with self.assertRaisesRegex(progress.ProgressError, "group weight must remain"):
            progress.validate(self.ledger, self.root)

    def test_lowering_four_player_and_machine_minima_cannot_fake_acceptance(self):
        criterion = self.ledger["groups"][1]["criteria"][-1]
        for players, machines in ((1, 1), (1, 4), (4, 1)):
            with self.subTest(players=players, machines=machines):
                criterion["min_players"] = players
                criterion["min_machines"] = machines
                self.accept(criterion)
                with self.assertRaisesRegex(progress.ProgressError, "baseline values must remain"):
                    progress.validate(self.ledger, self.root)

    def test_renaming_or_removing_criterion_ids_fails(self):
        criterion = self.first()
        criterion["id"] = "core-easier-replacement"
        with self.assertRaisesRegex(progress.ProgressError, "not in this baseline group"):
            progress.validate(self.ledger, self.root)
        criterion["id"] = "core-launcher"
        self.ledger["groups"][0]["criteria"].pop()
        with self.assertRaisesRegex(progress.ProgressError, "complete baseline criterion ID set"):
            progress.validate(self.ledger, self.root)

    def test_redistributing_weights_fails_even_when_group_and_grand_total_match(self):
        criteria = self.ledger["groups"][0]["criteria"]
        criteria[0]["weight"] += 1
        criteria[1]["weight"] -= 1
        self.assertEqual(10, sum(item["weight"] for item in criteria))
        self.assertEqual(100, sum(item["weight"] for group in self.ledger["groups"] for item in group["criteria"]))
        with self.assertRaisesRegex(progress.ProgressError, "baseline values must remain"):
            progress.validate(self.ledger, self.root)

    def test_moving_criteria_between_groups_fails_even_with_unchanged_weights(self):
        # Both weigh four points: exchanging them preserves every group's sum.
        core = self.ledger["groups"][0]["criteria"]
        actors = self.ledger["groups"][1]["criteria"]
        core[-1], actors[-1] = actors[-1], core[-1]
        self.assertEqual(10, sum(item["weight"] for item in core))
        self.assertEqual(20, sum(item["weight"] for item in actors))
        with self.assertRaisesRegex(progress.ProgressError, "not in this baseline group"):
            progress.validate(self.ledger, self.root)

    def test_renaming_baseline_without_a_schema_contract_revision_fails(self):
        self.ledger["baseline"] = "v1-easier"
        with self.assertRaisesRegex(progress.ProgressError, "unsupported baseline"):
            progress.validate(self.ledger, self.root)

    def test_github_validation_label_does_not_create_a_functional_status(self):
        self.first()["status"] = "validation"
        with self.assertRaisesRegex(progress.ProgressError, "invalid functional status"):
            progress.validate(self.ledger, self.root)
        self.first()["status"] = "in_progress"
        progress.validate(self.ledger, self.root)
        self.assertEqual(0, progress.percent(self.ledger))

    def test_preparation_cannot_carry_weight(self):
        self.ledger["preparation"][0]["weight"] = 1
        with self.assertRaisesRegex(progress.ProgressError, "preparation cannot carry"):
            progress.validate(self.ledger, self.root)

    def test_invalid_status_and_boolean_weight_fail(self):
        for status in ("cancelled", "ready", [], None):
            with self.subTest(status=status):
                self.first()["status"] = status
                with self.assertRaisesRegex(progress.ProgressError, "invalid functional status"):
                    progress.validate(self.ledger, self.root)
        self.first()["status"] = "planned"
        self.first()["weight"] = True
        with self.assertRaisesRegex(progress.ProgressError, "positive integer"):
            progress.validate(self.ledger, self.root)

    def test_verified_without_evidence_fails(self):
        self.first()["status"] = "verified"
        with self.assertRaisesRegex(progress.ProgressError, "verified requires evidence"):
            progress.validate(self.ledger, self.root)

    def test_full_commit_and_real_sha256_shape_are_required(self):
        criterion = self.accept()
        for field, invalid in (("commit", "abc1234"), ("commit", "0" * 40),
                               ("game_executable_sha256", "b" * 40),
                               ("game_executable_sha256", "z" * 64)):
            with self.subTest(field=field, invalid=invalid):
                criterion["evidence"] = self.valid_evidence()
                criterion["evidence"][field] = invalid
                with self.assertRaises(progress.ProgressError):
                    progress.validate(self.ledger, self.root)

    def test_evidence_needs_scenario_game_version_tester_and_valid_date(self):
        criterion = self.accept()
        for field in ("scenario", "game_version", "tested_by", "tested_on"):
            with self.subTest(missing=field):
                criterion["evidence"] = self.valid_evidence()
                del criterion["evidence"][field]
                with self.assertRaises(progress.ProgressError):
                    progress.validate(self.ledger, self.root)
        for invalid_date in ("20/10/2026", "2026-02-30"):
            with self.subTest(date=invalid_date):
                criterion["evidence"] = self.valid_evidence()
                criterion["evidence"]["tested_on"] = invalid_date
                with self.assertRaises(progress.ProgressError):
                    progress.validate(self.ledger, self.root)

    def test_four_player_criterion_rejects_two_players_or_one_machine(self):
        criterion = self.ledger["groups"][1]["criteria"][-1]
        self.accept(criterion)
        for players, machines in ((2, 2), (4, 1), (True, 4)):
            with self.subTest(players=players, machines=machines):
                criterion["evidence"]["participants"] = {"players": players, "machines": machines}
                with self.assertRaisesRegex(progress.ProgressError, "participants"):
                    progress.validate(self.ledger, self.root)

    def test_failed_manual_result_cannot_be_verified(self):
        criterion = self.accept()
        criterion["evidence"]["result"] = "failed"
        with self.assertRaisesRegex(progress.ProgressError, "only passed"):
            progress.validate(self.ledger, self.root)

    def test_report_must_be_local_nonempty_and_contained(self):
        criterion = self.accept()
        for report in ("../outside.md", "/outside.md", "C:\\private.md",
                       "https://example.test/report", "reports/missing.md"):
            with self.subTest(report=report):
                criterion["evidence"]["report"] = report
                with self.assertRaises(progress.ProgressError):
                    progress.validate(self.ledger, self.root)
        criterion["evidence"]["report"] = "reports/manual-test.md"
        (self.root / "reports" / "manual-test.md").write_text("", encoding="utf-8")
        with self.assertRaisesRegex(progress.ProgressError, "report is empty"):
            progress.validate(self.ledger, self.root)

    def test_generator_is_idempotent_and_preserves_readme_prose(self):
        self.assertEqual(0, self.run_cli())
        first = self.snapshot()
        self.assertEqual(0, self.run_cli())
        self.assertEqual(first, self.snapshot())
        generated = (self.root / "README.md").read_text(encoding="utf-8")
        self.assertTrue(generated.startswith("# Fixture\n\nText before.\n"))
        self.assertTrue(generated.endswith("\nText after.\n"))

    def test_check_never_creates_or_modifies_files(self):
        self.write_ledger()
        before = self.snapshot()
        self.assertEqual(1, self.run_cli("--check"))
        self.assertEqual(before, self.snapshot())
        self.assertEqual(0, self.run_cli())
        before = self.snapshot()
        self.assertEqual(0, self.run_cli("--check"))
        self.assertEqual(before, self.snapshot())

    def test_check_detects_each_stale_generated_view(self):
        self.assertEqual(0, self.run_cli())
        for relative in ("README.md", "PROGRESS.md", "docs/progress.svg"):
            with self.subTest(path=relative):
                path = self.root / relative
                content = path.read_text(encoding="utf-8")
                if relative == "README.md":
                    content = content.replace(progress.START, progress.START + "\nstale", 1)
                else:
                    content += "stale\n"
                path.write_text(content, encoding="utf-8")
                self.assertEqual(1, self.run_cli("--check"))
                self.assertEqual(0, self.run_cli())

    def test_missing_reversed_or_duplicate_markers_fail_without_partial_writes(self):
        for content in ("no markers", progress.END + progress.START,
                        progress.START + progress.START + progress.END):
            with self.subTest(content=content):
                (self.root / "README.md").write_text(content, encoding="utf-8")
                self.write_ledger()
                before = self.snapshot()
                self.assertEqual(1, self.run_cli())
                self.assertEqual(before, self.snapshot())

    def test_malformed_json_reports_failure_without_writing(self):
        (self.root / "progress.json").write_text("{broken", encoding="utf-8")
        before = self.snapshot()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = progress.main(["--root", str(self.root), "--check"])
        self.assertEqual(1, result)
        self.assertEqual(before, self.snapshot())


if __name__ == "__main__":
    unittest.main()
