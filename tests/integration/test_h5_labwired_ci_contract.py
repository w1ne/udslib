#!/usr/bin/env python3
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

"""Static contract for the release-backed dual-H5 LabWired CI gate."""

from __future__ import annotations

import hashlib
import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/nightly-h5-gate.yml"
CI_WORKFLOW = ROOT / ".github/workflows/ci.yml"
TESTER_FIRMWARE = ROOT / "examples/h5_uds_tester/firmware/main.c"
ECU_FIRMWARE = ROOT / "examples/h5_uds_ecu_full/firmware/main.c"

ACTION_SHA = "3a13349ad6c4f65b4fa19276f576bc3086b219e6"
DESCRIPTOR_SHA256 = "3e8f021058bcf58a93a1f3c8bfdd785802f9633d4ba52f869c2309874db64124"
DESCRIPTORS = (
    Path("examples/h5_uds_tester/stm32h563.yaml"),
    Path("examples/h5_uds_ecu_full/stm32h563.yaml"),
)


class H5LabWiredCiContractTest(unittest.TestCase):
    def test_fast_pr_gate_runs_this_contract(self) -> None:
        ci_workflow = CI_WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("Verify release-backed H5 LabWired CI contract", ci_workflow)
        self.assertIn(
            "run: python3 tests/integration/test_h5_labwired_ci_contract.py",
            ci_workflow,
        )

    def test_workflow_uses_immutable_release_action_without_a_core_build(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")

        expected_action = f"""uses: w1ne/labwired-core/.github/actions/labwired-test@{ACTION_SHA}
        with:
          version: v0.18.0
          script: examples/h5_uds_tester/allservices-gate.yaml
          output-dir: labwired-uds-report
          args: --no-uart-stdout"""
        self.assertIn(expected_action, workflow)
        self.assertRegex(
            workflow,
            r"uses:\s*w1ne/labwired-core/\.github/actions/labwired-test@[0-9a-f]{40}",
        )

        for forbidden in (
            "w1ne/labwired-core.git",
            "/tmp/labwired-core",
            "cargo build --release -p labwired-cli",
            "Swatinem/rust-cache@v2",
        ):
            self.assertNotIn(forbidden, workflow)

        self.assertIn("dtolnay/rust-toolchain@stable", workflow)
        self.assertIn('RUST_LLD="$(find "$(rustc --print sysroot)"', workflow)

    def test_h5_tester_checks_the_configured_p2_star_value(self) -> None:
        tester = TESTER_FIRMWARE.read_text(encoding="utf-8")
        ecu = ECU_FIRMWARE.read_text(encoding="utf-8")

        self.assertIn("cfg.p2_star_ms = 2000u;", tester)
        self.assertIn("cfg.p2_star_ms = 2000u;", ecu)
        self.assertIn("expected 50 03 00 32 00 C8", tester)
        self.assertRegex(
            tester,
            re.compile(
                r"g_resp_data\[3\] == 0x00u\s*&&\s*"
                r"g_resp_data\[4\] == 0xC8u"
            ),
        )
        self.assertNotIn("50 03 00 32 01 F4", tester)

    def test_h5_descriptors_are_release_exact_tracked_files(self) -> None:
        for descriptor in DESCRIPTORS:
            with self.subTest(descriptor=descriptor):
                descriptor_path = ROOT / descriptor
                self.assertTrue(descriptor_path.is_file())
                self.assertEqual(
                    hashlib.sha256(descriptor_path.read_bytes()).hexdigest(),
                    DESCRIPTOR_SHA256,
                )

                tracked = subprocess.run(
                    ["git", "ls-files", "--error-unmatch", "--", str(descriptor)],
                    cwd=ROOT,
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(tracked.returncode, 0, tracked.stderr)

                ignored = subprocess.run(
                    ["git", "check-ignore", "--quiet", "--", str(descriptor)],
                    cwd=ROOT,
                    check=False,
                )
                self.assertEqual(ignored.returncode, 1, f"{descriptor} must not be ignored")


if __name__ == "__main__":
    unittest.main()
