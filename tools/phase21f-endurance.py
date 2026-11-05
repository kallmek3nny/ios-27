#!/usr/bin/env python3
"""Explicitly bounded, no-idle multi-wave compiled concurrency endurance.

Runs the independent 240-second campaign repeatedly with compiler alternation.
Captures actual wall times and refuses to count a failed child as completed work.
"""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import shutil
import subprocess
import sys
import time

SCRIPT = Path(__file__).resolve().with_name("phase21f-campaign.py")

def utc():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=int, default=1200,
                        help="requested active test seconds, 1..3600")
    parser.add_argument("--wave-seconds", type=int, default=240,
                        help="seconds per bounded campaign, 1..240")
    args = parser.parse_args()
    if not 1 <= args.duration <= 3600 or not 1 <= args.wave_seconds <= 240:
        parser.error("duration must be 1..3600 and wave-seconds 1..240")
    compilers = [tool for tool in ("gcc", "clang") if shutil.which(tool)]
    if not compilers:
        parser.error("no native gcc or clang found")
    start = time.monotonic()
    started_utc = utc()
    print("endurance start UTC: " + started_utc, flush=True)
    wave = 0
    while time.monotonic() - start < args.duration:
        remaining = args.duration - (time.monotonic() - start)
        requested = max(1, min(args.wave_seconds, int(remaining) + 1))
        cc = compilers[wave % len(compilers)]
        wave += 1
        command = [sys.executable, str(SCRIPT), "--duration", str(requested),
                   "--compiler", cc]
        print("wave {} {} start {} duration {}s".format(wave, cc, utc(), requested),
              flush=True)
        try:
            result = subprocess.run(command, capture_output=True, text=True,
                                    timeout=requested + 60, check=False)
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError("wave {} exceeded hard child timeout: {}".format(wave, exc)) from exc
        if result.returncode:
            raise RuntimeError("wave {} failed: {}".format(wave, (result.stdout + result.stderr)[-5000:]))
        summary = next((line for line in reversed(result.stdout.splitlines())
                        if line.startswith("campaign PASS:")), None)
        if summary is None:
            raise RuntimeError("wave {} lacked explicit campaign PASS".format(wave))
        print("wave {} PASS {}".format(wave, summary), flush=True)
    print("endurance end UTC: " + utc(), flush=True)
    print("endurance actual elapsed: {:.2f}s, {} verified waves".format(
          time.monotonic() - start, wave), flush=True)

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as exc:
        sys.exit("phase21f endurance FAILED: {}".format(exc))
