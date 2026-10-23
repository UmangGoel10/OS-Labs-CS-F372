#!/usr/bin/env python3
"""Small command-file grader for Lab 6."""

import argparse
import difflib
import json
import os
import re
import selectors
import signal
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BUILD = ROOT / ".build" / "worst_fit"

ID_RE = re.compile(r"^\d{4}[A-Za-z0-9]*\d{4}G\.c$")

TIMEOUT = 5
OUTPUT_LIMIT = 1024 * 1024  # 1 MiB per output stream
DISPLAY_LIMIT = 2000        # characters shown for output-limit failures


def student_source():
    """Find the single student-ID C file."""
    matches = sorted(
        p for p in ROOT.glob("*.c")
        if ID_RE.fullmatch(p.name)
    )

    if not matches:
        raise RuntimeError(
            "No valid student-ID C file found. "
            "Rename student.c, for example: 2024A7PS0001G.c"
        )

    if len(matches) > 1:
        raise RuntimeError(
            "More than one valid student-ID C file found: "
            + ", ".join(p.name for p in matches)
        )

    return matches[0]


def build():
    """Compile the student's program."""
    source = student_source()
    BUILD.parent.mkdir(parents=True, exist_ok=True)

    result = subprocess.run(
        ["gcc", "-Wall", "-Wextra", "-g", str(source), "-o", str(BUILD)],
        text=True,
        capture_output=True,
    )

    # Always show compiler output.
    if result.stdout:
        print("--- Compiler stdout")
        print(result.stdout, end="")

    if result.stderr:
        print("--- Compiler stderr")
        print(result.stderr, end="")

    if result.returncode != 0:
        raise RuntimeError("Compilation failed.")

    print(f"Built {source.name}")


def selected(value, total):
    """Convert --n into a list of test numbers."""
    if total == 0:
        raise RuntimeError("No test cases found.")

    if not value:
        return list(range(1, total + 1))

    try:
        numbers = [int(x) for x in value.split()]
    except ValueError:
        raise RuntimeError(
            'N must contain test numbers, for example N=5 or N="1 2 3".'
        )

    if any(n < 1 or n > total for n in numbers):
        raise RuntimeError(f"Test numbers must be between 1 and {total}.")

    return numbers


def kill_process_group(pid):
    """Kill the process group if it still exists."""
    try:
        os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def run_program(case):
    """
    Run one test with a timeout and bounded output.

    The program gets its own process group so that children are also
    cleaned up if the program times out or exits unexpectedly.
    """
    p = subprocess.Popen(
        [str(BUILD), str(case)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )

    selector = selectors.DefaultSelector()
    selector.register(p.stdout, selectors.EVENT_READ, "stdout")
    selector.register(p.stderr, selectors.EVENT_READ, "stderr")

    output = {
        "stdout": bytearray(),
        "stderr": bytearray(),
    }

    deadline = time.monotonic() + TIMEOUT
    reason = None

    try:
        while selector.get_map():
            if p.poll() is None and time.monotonic() >= deadline:
                reason = f"timeout after {TIMEOUT} seconds"
                kill_process_group(p.pid)

            # Once the parent has exited, kill the group as well.
            # This handles children that may still hold the pipes open.
            if p.poll() is not None:
                kill_process_group(p.pid)

            events = selector.select(0.05)

            for key, _ in events:
                stream = key.data
                pipe = key.fileobj

                data = os.read(pipe.fileno(), 65536)

                if not data:
                    # EOF: this stream is finished. Stop polling it.
                    selector.unregister(pipe)
                    pipe.close()
                    continue

                output[stream] += data

                if len(output[stream]) > OUTPUT_LIMIT and reason is None:
                    reason = (
                        f"output-limit failure: {stream} exceeded "
                        f"{OUTPUT_LIMIT} bytes"
                    )
                    kill_process_group(p.pid)

        # Make sure the process is fully reaped.
        p.wait()

    except KeyboardInterrupt:
        kill_process_group(p.pid)

        # Close our pipe handles so the interrupt path cannot hang
        # waiting for descendants to close inherited descriptors.
        for key in list(selector.get_map().values()):
            key.fileobj.close()

        selector.close()
        p.wait()
        raise

    finally:
        selector.close()

    return (
        p.returncode,
        bytes(output["stdout"]),
        bytes(output["stderr"]),
        reason,
    )


def truncate_output(text, limit):
    """Return at most limit characters, with an omission notice."""
    if len(text) <= limit:
        return text

    omitted = len(text) - limit

    return (
        text[:limit]
        + f"\n... [{omitted} characters omitted] ..."
    )


def test(suite, choice):
    """Build the program and run the selected tests."""
    build()

    test_dir = ROOT / suite / "tests"
    solution_dir = ROOT / suite / "solutions"

    if not test_dir.is_dir():
        raise RuntimeError(f"Test directory not found: {test_dir}")

    if not solution_dir.is_dir():
        raise RuntimeError(f"Solution directory not found: {solution_dir}")

    cases = sorted(test_dir.glob("*.txt"))

    if not cases:
        raise RuntimeError(f"No test cases found in {test_dir}")

    numbers = selected(choice, len(cases))
    passed = 0

    for number in numbers:
        case = cases[number - 1]
        solution = solution_dir / f"{case.stem}.json"

        if not solution.is_file():
            raise RuntimeError(f"Missing solution file: {solution}")

        data = json.loads(solution.read_text())
        expected = data["expected_output"]

        code, stdout, stderr, reason = run_program(case)

        # Decode output safely.
        try:
            actual = stdout.decode("utf-8")
        except UnicodeDecodeError:
            reason = "invalid UTF-8 output"
            actual = stdout.decode("utf-8", errors="replace")

        try:
            stderr_text = stderr.decode("utf-8")
        except UnicodeDecodeError:
            stderr_text = stderr.decode("utf-8", errors="replace")

        # Determine the failure reason.
        if reason is None:
            if code < 0:
                try:
                    signal_name = signal.Signals(-code).name
                except ValueError:
                    signal_name = f"signal {-code}"

                reason = f"crash/signal: {signal_name}"

            elif code != 0:
                reason = f"nonzero exit status: {code}"

            elif actual != expected:
                reason = "output mismatch"

        passed_test = (
            code == 0
            and reason is None
            and actual == expected
        )

        if passed_test:
            passed += 1

        status = "PASS" if passed_test else "FAIL"

        print(f"\nTest {number}: {data['name']}: {status}")
        print(f"Input/test file: {case.relative_to(ROOT)}")

        if not passed_test:
            print(f"Failure reason: {reason}")

        # Output-limit failures get truncated output and no diff.
        output_limit_failure = (
            reason is not None
            and reason.startswith("output-limit failure:")
        )

        if output_limit_failure:
            print("--- Expected output")
            print(
                truncate_output(expected, DISPLAY_LIMIT),
                end="" if expected.endswith("\n") else "\n"
            )

            print("--- Your output")
            print(
                truncate_output(actual, DISPLAY_LIMIT),
                end="" if actual.endswith("\n") else "\n"
            )

            print("--- Unified diff")
            print("(diff omitted because output exceeded the limit)")

        else:
            # Normal output format, including ordinary failures.
            print("--- Expected output")
            print(
                expected,
                end="" if expected.endswith("\n") else "\n"
            )

            print("--- Your output")
            print(
                actual,
                end="" if actual.endswith("\n") else "\n"
            )

            print("--- Unified diff")

            diff = "".join(
                difflib.unified_diff(
                    expected.splitlines(True),
                    actual.splitlines(True),
                    fromfile="Expected",
                    tofile="Your output",
                )
            )

            if diff:
                print(diff, end="")

        if stderr_text:
            print("--- Program stderr")

            if output_limit_failure:
                stderr_display = truncate_output(
                    stderr_text,
                    DISPLAY_LIMIT,
                )
            else:
                stderr_display = stderr_text

            print(
                stderr_display,
                end="" if stderr_display.endswith("\n") else "\n"
            )

    print(f"\nSummary: {passed}/{len(numbers)} passed")

    return 0 if passed == len(numbers) else 1


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "action",
        choices=["build", "test"],
    )

    parser.add_argument(
        "--suite",
        choices=["public", "private"],
        default="public",
    )

    parser.add_argument(
        "--n",
        default="",
    )

    args = parser.parse_args()

    try:
        if args.action == "build":
            build()
            return 0

        return test(args.suite, args.n)

    except KeyboardInterrupt:
        print("\nInterrupted.", file=sys.stderr)
        return 130

    except (
        RuntimeError,
        OSError,
        json.JSONDecodeError,
    ) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())