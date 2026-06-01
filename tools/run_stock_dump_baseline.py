"""Stock pipeline baseline using rime_dump.exe."""
import argparse
import json
import subprocess
from pathlib import Path

from run_local_snapshot_baseline import build_steps, choose_rank, write_jsonl


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", default="stock_snapshot_test")
    parser.add_argument("--limit", type=int, default=300)
    parser.add_argument("--corpus",
                        default=r"C:\Code\outwit\witset-worker\tests\test_sentence.txt")
    parser.add_argument("--rime-dump",
                        default=r"C:\Code\outwit\outwit-windows\librime\build_x64\bin\Release\rime_dump.exe")
    parser.add_argument("--output",
                        default=r"C:\Users\Bing\AppData\Roaming\witty\debug\snapshot_summary\stock_bpe_reference_cases.jsonl")
    args = parser.parse_args()

    corpus_text = Path(args.corpus).read_text(encoding="utf-8")
    all_steps = build_steps(corpus_text, 72)
    text_steps = [s for s in all_steps if s.kind == "text"]

    if args.limit:
        allowed = {s.source_index for s in text_steps[:args.limit]}
        text_steps = [s for s in text_steps if s.source_index in allowed]

    print(f"Testing {len(text_steps)} text steps with schema '{args.schema}'")

    records = []
    found = 0
    not_found = 0

    for i, step in enumerate(text_steps):
        try:
            proc = subprocess.run(
                [args.rime_dump, args.schema, step.raw_input],
                capture_output=True, timeout=60,
                encoding="utf-8", errors="replace"
            )
        except subprocess.TimeoutExpired:
            print(f"  [{i + 1}] TIMEOUT: {step.raw_input}")
            candidates = []
            continue
        except Exception as e:
            print(f"  [{i + 1}] ERROR: {step.raw_input} -> {e}")
            candidates = []
            continue

        stdout = proc.stdout or ""
        try:
            candidates = json.loads(stdout.strip())
        except (json.JSONDecodeError, ValueError):
            candidates = []

        record = {
            "case_id": f"case_{i + 1:06d}",
            "input": step.raw_input,
            "expected_text": step.expected_text,
            "candidate_count": len(candidates),
        }

        # Check if expected text is in candidates
        expected_rank = None
        for ci, cand in enumerate(candidates):
            if cand.get("text", "") == step.expected_text:
                expected_rank = ci + 1
                break

        record["expected_rank"] = expected_rank

        if expected_rank is not None:
            found += 1
            if expected_rank == 1:
                record["top1_correct"] = True
        else:
            not_found += 1
            record["top1_correct"] = False

        records.append(record)

        if (i + 1) % 50 == 0:
            current_top1 = sum(1 for r in records if r.get("top1_correct"))
            print(f"  [{i + 1}/{len(text_steps)}] top1={current_top1}/{i + 1} = {current_top1 / (i + 1):.4f}")

    top1_count = sum(1 for r in records if r.get("top1_correct"))
    top3_count = sum(1 for r in records if r.get("expected_rank") is not None and r["expected_rank"] <= 3)

    print(f"\n=== Results ({args.schema}) ===")
    print(f"  Top-1: {top1_count}/{len(records)} = {top1_count / len(records):.4f}")
    print(f"  Top-3: {top3_count}/{len(records)} = {top3_count / len(records):.4f}")
    print(f"  Not found: {sum(1 for r in records if r.get('expected_rank') is None)}")
    print(f"  Found: {sum(1 for r in records if r.get('expected_rank') is not None)}")

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    write_jsonl(output, records)
    print(f"\nResults written to: {output}")


if __name__ == "__main__":
    main()
