#!/usr/bin/env python3
"""Headless simulator tests: every scenario's expectations pass, and every
screenshot is byte-for-byte identical across two runs (the UI is driven only
by the virtual clock). Run through ctest from the sim/ build."""

import argparse
import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile


# Per-scenario limit in seconds; raise it with TODO_SIM_TIMEOUT on slow machines.
TIMEOUT = float(os.environ.get("TODO_SIM_TIMEOUT", "120"))


def run(binary, scenario, out_dir):
    proc = subprocess.run(
        [binary, "--headless", "--scenario", str(scenario), "--out", str(out_dir)],
        capture_output=True,
        text=True,
        timeout=TIMEOUT,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        raise SystemExit(f"FAIL {scenario.name}: exit {proc.returncode}")
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out_dir.glob("*.png"))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--scenarios", required=True)
    args = ap.parse_args()

    scenarios = sorted(pathlib.Path(args.scenarios).glob("*.txt"))
    if not scenarios:
        raise SystemExit("no scenarios found")

    all_shots = {}
    for scenario in scenarios:
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            first = run(args.binary, scenario, pathlib.Path(a))
            second = run(args.binary, scenario, pathlib.Path(b))
        if first != second:
            diff = sorted(k for k in first if first.get(k) != second.get(k))
            raise SystemExit(f"FAIL {scenario.name}: screenshots differ between runs: {diff}")
        for name, digest in first.items():
            if name in all_shots:
                raise SystemExit(f"FAIL {scenario.name}: screenshot name {name} reused")
            all_shots[name] = digest
        print(f"ok   {scenario.name} ({len(first)} screenshots)")

    # Different states must look different.
    seen = {}
    for name, digest in all_shots.items():
        if digest in seen:
            raise SystemExit(f"FAIL {name} renders identically to {seen[digest]}")
        seen[digest] = name

    # Bad scenarios must fail loudly.
    with tempfile.TemporaryDirectory() as tmp:
        bad = pathlib.Path(tmp) / "bad.txt"
        for body in ("nonsense\n", "frobnicate=1\n", "set_list=[1]\n", "expect_screen=list\n"):
            bad.write_text(body)
            proc = subprocess.run([args.binary, "--headless", "--scenario", str(bad)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
            if proc.returncode == 0:
                raise SystemExit(f"FAIL bad scenario {body.strip()!r} was accepted")
    print(f"ok   {len(all_shots)} distinct screenshots, bad scenarios rejected")


if __name__ == "__main__":
    main()
