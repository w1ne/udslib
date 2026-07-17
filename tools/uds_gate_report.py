#!/usr/bin/env python3
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

"""
Dual-H5 all-services gate reporter.

The gate asserts one thing: the tester's 27-bit results word at 0x20010000
equals ALL_SERVICES_MASK. That is the right oracle, but it collapses 27
services into a single pass/fail bit -- a failing run reports
`expected 0x7ffffff, got 0x7fffffe` and leaves you to decode which service
bit 0 was.

The tester firmware already prints per-service evidence over UART. This turns
that transcript into structured output:

  * JUnit XML with one testcase per ISO-14229 service, so a test reporter
    shows 27 results instead of 1.
  * A Markdown table for $GITHUB_STEP_SUMMARY naming exactly which services
    failed, with the results bit each one owns.
  * A workflow annotation per failing service, so the run page names the
    culprit without downloading anything.

The service table is parsed out of service_bits.h rather than duplicated here,
so adding a service to the firmware cannot silently drift this report.

Usage:
    python3 tools/uds_gate_report.py \
        --uart labwired-uds-report/uart.log \
        --junit uds-service-report/junit-services.xml \
        --markdown uds-service-report/services.md
"""

import argparse
import os
import re
import sys
from xml.sax.saxutils import escape, quoteattr

DEFAULT_BITS_HEADER = "examples/h5_uds_tester/firmware/service_bits.h"

# `/* bit  4 */ #define BIT_22 (1u << 4u)  /* ReadDataByIdentifier  0x22 */`
BIT_DEF_RE = re.compile(
    r"#define\s+BIT_([0-9A-Fa-f]{2})\s*\(1u\s*<<\s*(\d+)u\)\s*/\*\s*(\S+)\s+0x([0-9A-Fa-f]{2})",
)
MASK_DEF_RE = re.compile(r"#define\s+ALL_SERVICES_MASK\s+0x([0-9A-Fa-f]+)u")

# The tester prints `TESTER_REQ_22`, then `{62F1}TESTER_RESP_62_OK`. Older
# firmware omits the `{...}` response-byte prefix, so it stays optional.
REQ_RE = re.compile(r"TESTER_REQ_([0-9A-F]{2})(?:_(SEED|KEY))?\b")
RESP_RE = re.compile(r"TESTER_RESP_([0-9A-F]{2})(?:_(SEED|KEY))?_(OK|BAD)\b")
TIMEOUT_RE = re.compile(r"TESTER_TIMEOUT_([0-9A-F]{2})\b")
EVENT_RE = re.compile(
    r"(?:\{(?P<bytes>[0-9A-F]+)\})?"
    r"(?P<event>TESTER_REQ_[0-9A-F]{2}(?:_(?:SEED|KEY))?"
    r"|TESTER_RESP_[0-9A-F]{2}(?:_(?:SEED|KEY))?_(?:OK|BAD)"
    r"|TESTER_TIMEOUT_[0-9A-F]{2})"
)


class DriftError(RuntimeError):
    """The transcript or header no longer matches what this reporter expects."""


def load_services(header_path):
    """Parse service_bits.h -> ({sid: {...}}, all_services_mask)."""
    try:
        with open(header_path) as fh:
            text = fh.read()
    except OSError as exc:
        raise DriftError(f"cannot read service table {header_path}: {exc}")

    services = {}
    for m in BIT_DEF_RE.finditer(text):
        macro_sid, bit, name, comment_sid = m.group(1), int(m.group(2)), m.group(3), m.group(4)
        if macro_sid.upper() != comment_sid.upper():
            raise DriftError(
                f"service_bits.h: BIT_{macro_sid} is documented as 0x{comment_sid}"
            )
        sid = int(macro_sid, 16)
        if sid in services:
            raise DriftError(f"service_bits.h: duplicate entry for 0x{sid:02X}")
        services[sid] = {"sid": sid, "bit": bit, "name": name}

    if not services:
        raise DriftError(f"no BIT_xx service definitions found in {header_path}")

    mask_m = MASK_DEF_RE.search(text)
    if not mask_m:
        raise DriftError(f"ALL_SERVICES_MASK not found in {header_path}")
    all_mask = int(mask_m.group(1), 16)

    derived = 0
    for svc in services.values():
        derived |= 1 << svc["bit"]
    if derived != all_mask:
        raise DriftError(
            f"service_bits.h is inconsistent: bits give 0x{derived:08X}, "
            f"ALL_SERVICES_MASK is 0x{all_mask:08X}"
        )
    return services, all_mask


def tester_transcript(uart_path):
    """Return only the tester node's section of the UART log."""
    try:
        with open(uart_path, errors="replace") as fh:
            text = fh.read()
    except OSError as exc:
        raise DriftError(f"cannot read UART log {uart_path}: {exc}")

    # uart.log is `[node:<id>]` sections; the ECU half is noise for this report.
    sections = re.split(r"^\[node:([^\]]+)\]$", text, flags=re.M)
    if len(sections) > 1:
        for i in range(1, len(sections), 2):
            if sections[i].strip() == "tester":
                return sections[i + 1]
        raise DriftError(f"no [node:tester] section in {uart_path}")
    if "TESTER_REQ_" not in text:
        raise DriftError(f"{uart_path} contains no tester transcript")
    return text


def parse_attempts(transcript, services):
    """Walk the transcript, pairing each request with its outcome."""
    attempts = {sid: [] for sid in services}
    pending = None

    for m in EVENT_RE.finditer(transcript):
        event, resp_bytes = m.group("event"), m.group("bytes")

        req = REQ_RE.fullmatch(event)
        if req:
            sid = int(req.group(1), 16)
            if sid not in services:
                raise DriftError(f"transcript requests unknown service 0x{sid:02X}")
            pending = (sid, req.group(2))
            continue

        resp = RESP_RE.fullmatch(event)
        if resp:
            if pending is None:
                raise DriftError(f"response {event} with no preceding request")
            sid, step = pending
            attempts[sid].append({
                "step": step,
                "outcome": "OK" if resp.group(3) == "OK" else "BAD",
                "evidence": (f"{{{resp_bytes}}}" if resp_bytes else "") + event,
            })
            pending = None
            continue

        timeout = TIMEOUT_RE.fullmatch(event)
        if timeout:
            sid = int(timeout.group(1), 16)
            if sid not in services:
                raise DriftError(f"transcript times out on unknown service 0x{sid:02X}")
            step = pending[1] if pending and pending[0] == sid else None
            attempts[sid].append({"step": step, "outcome": "TIMEOUT", "evidence": event})
            pending = None

    return attempts


def evaluate(services, attempts):
    """Decide pass/fail per service and rebuild the results word."""
    results, observed = [], 0
    for sid in sorted(services, key=lambda s: services[s]["bit"]):
        svc = services[sid]
        tries = attempts[sid]
        if not tries:
            status, detail = "not-reached", "service was never requested (run aborted early?)"
        elif all(t["outcome"] == "OK" for t in tries):
            status, detail = "pass", " ".join(t["evidence"] for t in tries)
        else:
            status = "fail"
            bad = [t for t in tries if t["outcome"] != "OK"]
            detail = "; ".join(
                f"{'seed/key '+t['step'].lower()+': ' if t['step'] else ''}{t['evidence']}"
                for t in bad
            )
        if status == "pass":
            observed |= 1 << svc["bit"]
        results.append({**svc, "status": status, "detail": detail, "attempts": tries})
    return results, observed


def write_junit(path, results, suite_name):
    failures = sum(1 for r in results if r["status"] != "pass")
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<testsuite name={quoteattr(suite_name)} tests="{len(results)}" '
        f'failures="{failures}" errors="0">',
    ]
    for r in results:
        name = f"0x{r['sid']:02X} {r['name']}"
        lines.append(
            f'  <testcase classname={quoteattr(suite_name + ".services")} '
            f'name={quoteattr(name)}>'
        )
        if r["status"] != "pass":
            msg = f"results bit {r['bit']} not set — {r['detail']}"
            lines.append(
                f'    <failure message={quoteattr(msg)} type={quoteattr(r["status"])}>'
                f'{escape(r["detail"])}</failure>'
            )
        lines.append("  </testcase>")
    lines.append("</testsuite>")
    _write(path, "\n".join(lines) + "\n")
    return failures


def write_markdown(path, results, observed, expected):
    passed = sum(1 for r in results if r["status"] == "pass")
    ok = observed == expected
    out = [
        f"## UDS services — {passed}/{len(results)} passed",
        "",
        f"- Results word: `0x{observed:08X}`"
        + ("" if ok else f" (expected `0x{expected:08X}`)"),
    ]
    if not ok:
        missing = [r for r in results if r["status"] != "pass"]
        out.append(f"- Missing bits: `0x{expected & ~observed:08X}`")
        out.append("")
        out.append("### Failing services")
        out.append("")
        out.append("| SID | Service | Bit | Evidence |")
        out.append("|---|---|---|---|")
        for r in missing:
            out.append(
                f"| `0x{r['sid']:02X}` | {r['name']} | {r['bit']} | `{r['detail']}` |"
            )
    out += ["", "### All services", "", "| SID | Service | Bit | Result |", "|---|---|---|---|"]
    for r in results:
        mark = {"pass": "✅ pass", "fail": "❌ fail", "not-reached": "⚠️ not reached"}[r["status"]]
        out.append(f"| `0x{r['sid']:02X}` | {r['name']} | {r['bit']} | {mark} |")
    out.append("")
    _write(path, "\n".join(out) + "\n")


def _write(path, text):
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, "w") as fh:
        fh.write(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--uart", required=True, help="uart.log from the LabWired run")
    ap.add_argument("--bits-header", default=DEFAULT_BITS_HEADER,
                    help=f"service table to decode against (default: {DEFAULT_BITS_HEADER})")
    ap.add_argument("--junit", help="write per-service JUnit XML here")
    ap.add_argument("--markdown", help="write a Markdown summary here")
    ap.add_argument("--suite-name", default="uds-h5-gate", help="JUnit testsuite name")
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero when services are missing (default: report only)")
    args = ap.parse_args()

    try:
        services, expected = load_services(args.bits_header)
        transcript = tester_transcript(args.uart)
        attempts = parse_attempts(transcript, services)
    except DriftError as exc:
        # Loud on drift: a silently empty report would read as "nothing failed".
        print(f"::error::uds_gate_report: {exc}", file=sys.stderr)
        return 2

    results, observed = evaluate(services, attempts)
    passed = sum(1 for r in results if r["status"] == "pass")

    if args.junit:
        write_junit(args.junit, results, args.suite_name)
    if args.markdown:
        write_markdown(args.markdown, results, observed, expected)
        # Put the table on the run page itself, so a failure is readable
        # without downloading the artifact.
        step_summary = os.environ.get("GITHUB_STEP_SUMMARY")
        if step_summary:
            with open(args.markdown) as src, open(step_summary, "a") as dst:
                dst.write(src.read())

    print(f"UDS services: {passed}/{len(results)} passed")
    print(f"results word: 0x{observed:08X} (expected 0x{expected:08X})")
    annotate = os.environ.get("GITHUB_ACTIONS") == "true"
    for r in results:
        if r["status"] == "pass":
            continue
        line = f"bit {r['bit']} not set — {r['detail']}"
        print(f"  FAIL bit {r['bit']:>2}  0x{r['sid']:02X} {r['name']}: {r['detail']}")
        if annotate:
            print(f"::error title=UDS 0x{r['sid']:02X} {r['name']}::{line}")

    return 1 if (args.strict and observed != expected) else 0


if __name__ == "__main__":
    sys.exit(main())
