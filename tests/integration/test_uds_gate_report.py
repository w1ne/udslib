#!/usr/bin/env python3
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

"""Contract for the per-service reporter that decodes the dual-H5 gate.

The gate asserts one 27-bit word. This reporter is what turns that word back
into named services, so its two failure modes both matter: reporting a green
run as broken, and -- far worse -- reporting a broken run as green because the
UART format drifted underneath it.
"""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BITS_HEADER = ROOT / "examples/h5_uds_tester/firmware/service_bits.h"
WORKFLOW = ROOT / ".github/workflows/nightly-h5-gate.yml"
CI_WORKFLOW = ROOT / ".github/workflows/ci.yml"

_spec = importlib.util.spec_from_file_location(
    "uds_gate_report", ROOT / "tools/uds_gate_report.py"
)
report = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(report)


def transcript(body: str) -> str:
    return "[node:ecu]\nH5-UDS-ECU-FULL\nECU_READY\n[node:tester]\nH5-UDS-TESTER\n" + body


def full_run(overrides: dict[int, str] | None = None) -> str:
    """A transcript where every service answers OK, minus any overrides."""
    overrides = overrides or {}
    services, _ = report.load_services(BITS_HEADER)
    lines = []
    for sid in sorted(services):
        if sid in overrides:
            lines.append(overrides[sid])
            continue
        resp = (sid + 0x40) & 0xFF
        if sid == 0x27:  # seed + key: two exchanges, one bit
            lines += [
                "TESTER_REQ_27_SEED", f"{{{resp:02X}01}}TESTER_RESP_{resp:02X}_SEED_OK",
                "TESTER_REQ_27_KEY", f"{{{resp:02X}02}}TESTER_RESP_{resp:02X}_KEY_OK",
            ]
        else:
            lines += [f"TESTER_REQ_{sid:02X}", f"{{{resp:02X}00}}TESTER_RESP_{resp:02X}_OK"]
    return transcript("\n".join(lines) + "\n")


def run(text: str, tmp: Path):
    uart = tmp / "uart.log"
    uart.write_text(text)
    services, expected = report.load_services(BITS_HEADER)
    attempts = report.parse_attempts(report.tester_transcript(str(uart)), services)
    results, observed = report.evaluate(services, attempts)
    return results, observed, expected


class ServiceTableTest(unittest.TestCase):
    def test_table_is_derived_from_the_firmware_header(self) -> None:
        services, mask = report.load_services(BITS_HEADER)

        # 27 ISO-14229 services, bits 0..26, mask matching ALL_SERVICES_MASK.
        self.assertEqual(len(services), 27)
        self.assertEqual(mask, 0x07FFFFFF)
        self.assertEqual(sorted(s["bit"] for s in services.values()), list(range(27)))
        self.assertEqual(services[0x22]["name"], "ReadDataByIdentifier")
        self.assertEqual(services[0x10]["bit"], 0)
        self.assertEqual(services[0x11]["name"], "EcuReset")

    def test_inconsistent_header_is_rejected(self) -> None:
        bad = "#define BIT_10 (1u << 0u) /* DiagnosticSessionControl 0x10 */\n" \
              "#define ALL_SERVICES_MASK 0x07FFFFFFu\n"
        path = Path(self.enterContext(__import__("tempfile").TemporaryDirectory())) / "b.h"
        path.write_text(bad)
        with self.assertRaises(report.DriftError):
            report.load_services(path)


class DecodeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(self.enterContext(__import__("tempfile").TemporaryDirectory()))

    def test_all_ok_rebuilds_the_expected_results_word(self) -> None:
        results, observed, expected = run(full_run(), self.tmp)

        self.assertEqual(observed, expected)
        self.assertEqual(observed, 0x07FFFFFF)
        self.assertTrue(all(r["status"] == "pass" for r in results))

    def test_a_bad_response_clears_exactly_that_services_bit(self) -> None:
        # Reproduces real run 29307549094: 0x10 answered BAD -> got 0x7fffffe.
        text = full_run({0x10: "TESTER_REQ_10\n{5003}TESTER_RESP_50_BAD"})
        results, observed, _ = run(text, self.tmp)

        self.assertEqual(observed, 0x07FFFFFE)
        failed = [r for r in results if r["status"] != "pass"]
        self.assertEqual(len(failed), 1)
        self.assertEqual(failed[0]["sid"], 0x10)
        self.assertEqual(failed[0]["bit"], 0)
        self.assertIn("TESTER_RESP_50_BAD", failed[0]["detail"])

    def test_a_timeout_is_a_failure_not_a_pass(self) -> None:
        text = full_run({0x22: "TESTER_REQ_22\nTESTER_TIMEOUT_22"})
        results, observed, _ = run(text, self.tmp)

        self.assertEqual(observed & (1 << 4), 0)
        failed = [r for r in results if r["status"] != "pass"]
        self.assertEqual([r["sid"] for r in failed], [0x22])

    def test_security_access_needs_both_seed_and_key(self) -> None:
        text = full_run({
            0x27: "TESTER_REQ_27_SEED\n{6701}TESTER_RESP_67_SEED_OK\n"
                  "TESTER_REQ_27_KEY\n{6702}TESTER_RESP_67_KEY_BAD",
        })
        results, observed, _ = run(text, self.tmp)

        self.assertEqual(observed & (1 << 7), 0)
        self.assertEqual([r["sid"] for r in results if r["status"] != "pass"], [0x27])

    def test_a_truncated_run_reports_not_reached_rather_than_pass(self) -> None:
        text = transcript("TESTER_REQ_10\n{5003}TESTER_RESP_50_OK\n")
        results, observed, expected = run(text, self.tmp)

        self.assertEqual(observed, 0x00000001)
        self.assertNotEqual(observed, expected)
        unreached = [r for r in results if r["status"] == "not-reached"]
        self.assertEqual(len(unreached), 26)

    def test_response_byte_prefix_is_optional(self) -> None:
        # Firmware before the {sid,data0} DIAG prefix logged bare tokens.
        text = full_run({0x10: "TESTER_REQ_10\nTESTER_RESP_50_OK"})
        _, observed, expected = run(text, self.tmp)

        self.assertEqual(observed, expected)


class DriftTest(unittest.TestCase):
    """A format change must fail loudly, never silently report a green run."""

    def setUp(self) -> None:
        self.tmp = Path(self.enterContext(__import__("tempfile").TemporaryDirectory()))

    def test_missing_tester_section_is_an_error(self) -> None:
        uart = self.tmp / "uart.log"
        uart.write_text("[node:ecu]\nH5-UDS-ECU-FULL\nECU_READY\n")
        with self.assertRaises(report.DriftError):
            report.tester_transcript(str(uart))

    def test_unknown_service_in_transcript_is_an_error(self) -> None:
        services, _ = report.load_services(BITS_HEADER)
        with self.assertRaises(report.DriftError):
            report.parse_attempts(transcript("TESTER_REQ_99\n"), services)

    def test_response_without_a_request_is_an_error(self) -> None:
        services, _ = report.load_services(BITS_HEADER)
        with self.assertRaises(report.DriftError):
            report.parse_attempts(transcript("{5003}TESTER_RESP_50_OK\n"), services)


class WorkflowWiringTest(unittest.TestCase):
    def test_fast_pr_gate_runs_this_contract(self) -> None:
        ci_workflow = CI_WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("Verify per-service UDS gate reporter", ci_workflow)
        self.assertIn(
            "run: python3 tests/integration/test_uds_gate_report.py", ci_workflow
        )

    def test_nightly_gate_publishes_the_per_service_report(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("python3 tools/uds_gate_report.py", workflow)
        self.assertIn("--uart labwired-uds-report/uart.log", workflow)
        self.assertIn("--junit uds-service-report/junit-services.xml", workflow)
        self.assertIn("--markdown uds-service-report/services.md", workflow)
        # Must still run when the gate itself fails -- that is the case it exists for.
        decode = workflow.split("Decode per-service UDS results", 1)[1]
        self.assertIn("if: ${{ always() }}", decode.split("run:", 1)[0])
        self.assertIn("name: uds-service-report-${{ github.run_id }}", workflow)
        self.assertIn("retention-days: 90", workflow)


if __name__ == "__main__":
    unittest.main(verbosity=2)
