"""Protect one-shot stable release dispatch from regressions and shell errors.

Contract ensures new `workflow_run` trigger dispatches only successful exact
main push CI and cannot silently bypass the A/V packaging smoke pipeline.
"""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/stable-v010-once.yml"
RELEASE = ROOT / ".github/workflows/windows-release-exes.yml"


class OneShotStableDispatch(unittest.TestCase):
    def test_is_only_triggered_by_successful_same_repo_main_ci(self):
        script = WORKFLOW.read_text(encoding="utf-8")
        for phrase in (
            "workflow_run:",
            "- CI",
            "- completed",
            "github.event.workflow_run.conclusion == 'success'",
            "github.event.workflow_run.head_branch == 'main'",
            "github.event.workflow_run.event == 'push'",
            "github.event.workflow_run.head_repository.full_name == github.repository",
            "concurrency:",
            "cancel-in-progress: false",
            '"$CI_SHA" != "$main_sha"',
            'gh api "repos/$GH_REPO/git/ref/tags/v0.1.0"',
            "Stable release packaging already queued/running/succeeded",
            "gh workflow run windows-release-exes.yml",
            '-f source_run_id="$CI_RUN_ID"',
            '-f version="0.1.0"',
            '-f publish_prerelease="false"',
            '-f publish_stable="true"',
        ):
            with self.subTest(phrase=phrase):
                self.assertIn(phrase, script)

    def test_no_publication_bypass(self):
        dispatch = WORKFLOW.read_text(encoding="utf-8")
        release = RELEASE.read_text(encoding="utf-8")
        self.assertNotIn("gh release create", dispatch)
        self.assertNotIn("gh release upload", dispatch)
        self.assertNotIn("on:\n  push:", dispatch)
        for expected in (
            "Verify exact-head audio CI provenance",
            "Smoke test EXACT portable and installed Setup EXE on Windows",
            "Publish verified GitHub release",
            "Source is not successful exact-head main CI",
            "Expected exactly two executables plus SHA256SUMS",
        ):
            self.assertIn(expected, release)

    def test_dispatch_script_parses_as_bash(self):
        contents = WORKFLOW.read_text(encoding="utf-8")
        marker = "        run: |\n"
        self.assertEqual(contents.count(marker), 1)
        source = contents.split(marker, 1)[1]
        lines = []
        for line in source.splitlines():
            if line.startswith("          "):
                lines.append(line[10:])
            elif not line.strip():
                lines.append("")
            else:
                break
        self.assertGreater(len(lines), 20)
        executable = shutil.which("bash")
        if executable is None:
            self.skipTest("Git Bash is not installed on this developer host")
        completed = subprocess.run(
            [executable, "-n"], input="\n".join(lines),
            text=True, capture_output=True, check=False)
        self.assertEqual(completed.returncode, 0, completed.stderr)


if __name__ == "__main__":
    unittest.main()
