from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
TOOLS_ROOT = REPO_ROOT / "tools"


class WindowsSourceContractTests(unittest.TestCase):
    def test_contract_rejects_stale_dirty_inputs(self) -> None:
        completed = subprocess.run(
            [
                "powershell.exe",
                "-NoLogo",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(TOOLS_ROOT / "test_windows_source_contract.ps1"),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=120,
            check=False,
        )
        self.assertEqual(
            completed.returncode,
            0,
            msg=f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}",
        )
        self.assertIn("PASS windows source contract", completed.stdout)

    def test_build_and_runtime_scripts_enforce_the_contract(self) -> None:
        build = (TOOLS_ROOT / "build_windows_source.ps1").read_text("utf-8")
        prepare = (
            TOOLS_ROOT / "prepare_windows_client_runtime.ps1"
        ).read_text("utf-8")

        rebuild_target = build.index('$target = "Rebuild"')
        target_argument = build.index('"/t:$target"')
        before = build.index(
            "$sourceContractBefore = New-OpenWydWindowsSourceContract"
        )
        invoke = build.index("& $msbuild @msbuildArguments")
        after = build.index(
            "$sourceContractAfter = New-OpenWydWindowsSourceContractFromManifest"
        )
        bind = build.index(
            "$provenanceBindingSha256 = "
            "Get-OpenWydWindowsProvenanceBindingSha256"
        )
        certified = build.index('$metadata.sourceBuildCertified = $true')
        self.assertLess(rebuild_target, target_argument)
        self.assertLess(target_argument, before)
        self.assertLess(before, invoke)
        self.assertLess(invoke, after)
        self.assertLess(after, bind)
        self.assertLess(bind, certified)
        self.assertNotIn('$target = if ($Clean)', build)
        self.assertNotIn('"/t:Build"', build)
        self.assertIn(
            "Certified builds always\nrun the MSBuild Rebuild target",
            build,
        )
        self.assertIn(
            "$sourceContractValidation = Assert-OpenWydWindowsSourceContract",
            prepare,
        )
        self.assertIn(
            "$provenanceBindingSha256 = "
            "Get-OpenWydWindowsProvenanceBindingSha256",
            prepare,
        )


if __name__ == "__main__":
    unittest.main()
