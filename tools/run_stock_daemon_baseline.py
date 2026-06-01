"""Stock pipeline baseline using rime_dumpd daemon (single session, supports preceding text)."""
import argparse
import json
import subprocess
from pathlib import Path
from typing import Optional

from run_local_snapshot_baseline import build_steps, write_jsonl


def run_daemon_baseline(schema: str, steps: list, dumpd_path: str) -> list[dict]:
    """Run baseline using the rime_dumpd daemon protocol."""
    proc = subprocess.Popen(
        [dumpd_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )

    # Wait for READY
    stderr_line = proc.stderr.readline()
    if "READY" not in stderr_line:
        raise RuntimeError(f"Daemon not ready: {stderr_line}")

    # Select schema
    proc.stdin.write(f"S {schema}\n")
    proc.stdin.flush()

    records = []
    preceding_text = ""

    for i, step in enumerate(steps):
        if step.kind == "text":
            if preceding_text:
                proc.stdin.write(f"P {preceding_text}\n")
            else:
                proc.stdin.write("C\n")
            proc.stdin.write(f"D {step.raw_input}\n")
            proc.stdin.flush()

            # Read JSON response
            line = proc.stdout.readline().strip()
            candidates = json.loads(line) if line.startswith("[") else []

            expected_rank = None
            for ci, cand in enumerate(candidates):
                if cand.get("text", "") == step.expected_text:
                    expected_rank = ci + 1
                    break

            record = {
                "case_id": f"case_{len(records) + 1:06d}",
                "input": step.raw_input,
                "preceding_text": preceding_text,
                "expected_text": step.expected_text,
                "candidate_count": len(candidates),
                "expected_rank": expected_rank,
                "top1_correct": (expected_rank == 1),
            }
            records.append(record)

            preceding_text = step.expected_text

        if (len(records)) % 50 == 0 and len(records) > 0:
            top1 = sum(1 for r in records if r.get("top1_correct"))
            print(f"  [{len(records)}] top1={top1}/{len(records)} = {top1 / len(records):.4f}")

    proc.stdin.write("X\n")
    proc.stdin.flush()
    proc.wait(timeout=5)

    return records


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", default="stock_snapshot_test")
    parser.add_argument("--limit", type=int, default=300)
    parser.add_argument("--corpus",
                        default=r"C:\Code\outwit\witset-worker\tests\test_sentence.txt")
    parser.add_argument("--dumpd",
                        default=r"C:\Code\outwit\outwit-windows\librime\build_x64\bin\Release\rime_dumpd.exe")
    parser.add_argument("--output",
                        default=r"C:\Users\Bing\AppData\Roaming\witty\debug\snapshot_summary\stock_daemon_reference_cases.jsonl")
    args = parser.parse_args()

    corpus_text = Path(args.corpus).read_text(encoding="utf-8")
    all_steps = build_steps(corpus_text, 72)
    text_steps = [s for s in all_steps if s.kind == "text"]

    if args.limit:
        allowed = {s.source_index for s in text_steps[:args.limit]}
        all_steps = [s for s in all_steps
                     if s.kind != "text" or s.source_index in allowed]
        text_steps = [s for s in all_steps if s.kind == "text"]

    print(f"Testing {len(text_steps)} text steps with schema '{args.schema}' (daemon mode)")

    records = run_daemon_baseline(args.schema, all_steps, args.dumpd)

    top1 = sum(1 for r in records if r.get("top1_correct"))
    top3 = sum(1 for r in records
               if r.get("expected_rank") is not None and r["expected_rank"] <= 3)
    not_found = sum(1 for r in records if r.get("expected_rank") is None)

    print(f"\n=== Results ({args.schema}) ===")
    print(f"  Top-1: {top1}/{len(records)} = {top1 / len(records):.4f}")
    print(f"  Top-3: {top3}/{len(records)} = {top3 / len(records):.4f}")
    print(f"  Not found: {not_found}")
    print(f"  Found: {len(records) - not_found}")

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    write_jsonl(output, records)
    print(f"\nResults written to: {output}")


if __name__ == "__main__":
    main()
