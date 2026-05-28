#!/usr/bin/env python3
"""Summarize witset local snapshot JSONL records."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path
from statistics import mean
from typing import Any


DEBUG_VALUE_RE = re.compile(r"([A-Za-z]+):(-?\d+(?:\.\d+)?)")
INPUT_KEYS = ("input", "raw_input", "query", "keys")
TEXT_KEYS = ("expected_text", "reference_text", "target_text", "expected", "text")
PRECEDING_KEYS = ("preceding_text", "context", "prefix", "preceding")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Summarize witset local snapshot JSONL records."
    )
    parser.add_argument(
        "--snapshot",
        required=True,
        help="Path to the local snapshot JSONL file.",
    )
    parser.add_argument(
        "--output-dir",
        help="Directory for metrics.json and regression_report.md. "
        "Defaults to <snapshot_dir>/snapshot_summary.",
    )
    parser.add_argument(
        "--reference",
        help="Optional JSON or JSONL file with input-to-reference mapping.",
    )
    return parser.parse_args()


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
      for line_number, raw_line in enumerate(handle, start=1):
        line = raw_line.strip()
        if not line:
            continue
        try:
            data = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(
                f"Failed to parse JSONL line {line_number} from {path}: {exc}"
            ) from exc
        if not isinstance(data, dict):
            raise ValueError(
                f"JSONL line {line_number} from {path} is not an object."
            )
        records.append(data)
    return records


def parse_debug(debug_text: str) -> dict[str, float]:
    values: dict[str, float] = {}
    for key, raw_value in DEBUG_VALUE_RE.findall(debug_text or ""):
        values[key] = float(raw_value)
    return values


def find_first_value(record: dict[str, Any], keys: tuple[str, ...]) -> str | None:
    for key in keys:
        value = record.get(key)
        if isinstance(value, str) and value:
            return value
    return None


def make_composite_key(input_text: str, preceding_text: str) -> str:
    return f"{preceding_text}\n{input_text}"


def normalize_reference_payload(payload: Any) -> list[dict[str, Any]]:
    if isinstance(payload, list):
        return [item for item in payload if isinstance(item, dict)]
    if isinstance(payload, dict):
        if isinstance(payload.get("cases"), list):
            return [item for item in payload["cases"] if isinstance(item, dict)]
        if isinstance(payload.get("records"), list):
            return [item for item in payload["records"] if isinstance(item, dict)]
        if all(isinstance(key, str) for key in payload.keys()):
            records: list[dict[str, Any]] = []
            for key, value in payload.items():
                if isinstance(value, str):
                    records.append({"input": key, "expected_text": value})
                elif isinstance(value, dict):
                    item = dict(value)
                    item.setdefault("input", key)
                    records.append(item)
            return records
    raise ValueError("Unsupported reference payload format.")


def load_reference_map(path: Path | None) -> tuple[dict[str, str], dict[str, str]]:
    if path is None:
        return {}, {}

    if path.suffix.lower() == ".jsonl":
        records = load_jsonl(path)
    else:
        records = normalize_reference_payload(load_json(path))

    composite_mapping: dict[str, str] = {}
    input_only_mapping: dict[str, str] = {}
    for record in records:
        input_text = find_first_value(record, INPUT_KEYS)
        preceding_text = find_first_value(record, PRECEDING_KEYS) or ""
        expected_text = find_first_value(record, TEXT_KEYS)
        if input_text and expected_text:
            composite_mapping[make_composite_key(input_text, preceding_text)] = expected_text
            input_only_mapping[input_text] = expected_text
    return composite_mapping, input_only_mapping


def safe_mean(values: list[float]) -> float | None:
    if not values:
        return None
    return mean(values)


def round_or_none(value: float | None, digits: int = 6) -> float | None:
    if value is None:
        return None
    return round(value, digits)


def build_summary(
    records: list[dict[str, Any]],
    reference_by_composite_key: dict[str, str],
    reference_by_input: dict[str, str],
) -> tuple[dict[str, Any], dict[str, dict[str, Any]], list[dict[str, Any]]]:
    latest_by_key: dict[str, dict[str, Any]] = {}
    history_by_key: dict[str, list[str]] = {}
    top1_counter: Counter[str] = Counter()
    candidate_counts: list[int] = []
    top1_total_values: list[float] = []
    top1_dict_values: list[float] = []
    top1_dict_norm_values: list[float] = []
    top1_lm_scaled_values: list[float] = []
    top1_lm_avg_values: list[float] = []
    top1_boundary_values: list[float] = []
    top1_oov_values: list[float] = []
    top1_length_values: list[float] = []
    top1_whole_values: list[float] = []
    records_with_candidates = 0
    records_with_debug = 0

    for record in records:
        input_text = str(record.get("input", ""))
        preceding_text = str(record.get("preceding_text", ""))
        record_key = make_composite_key(input_text, preceding_text)
        candidates = record.get("candidates") or []
        if not isinstance(candidates, list):
            candidates = []
        candidate_count = int(record.get("candidate_count", len(candidates)) or 0)
        candidate_counts.append(candidate_count)
        if candidate_count > 0:
            records_with_candidates += 1

        top1_text = ""
        if candidates:
            first = candidates[0] if isinstance(candidates[0], dict) else {}
            top1_text = str(first.get("text", ""))
            debug_values = parse_debug(str(first.get("debug", "")))
            if debug_values:
                records_with_debug += 1
                if "Total" in debug_values:
                    top1_total_values.append(debug_values["Total"])
                if "Dict" in debug_values:
                    top1_dict_values.append(debug_values["Dict"])
                if "DictNorm" in debug_values:
                    top1_dict_norm_values.append(debug_values["DictNorm"])
                if "LmScaled" in debug_values:
                    top1_lm_scaled_values.append(debug_values["LmScaled"])
                if "LmAvg" in debug_values:
                    top1_lm_avg_values.append(debug_values["LmAvg"])
                if "Boundary" in debug_values:
                    top1_boundary_values.append(debug_values["Boundary"])
                if "OOV" in debug_values:
                    top1_oov_values.append(debug_values["OOV"])
                if "Len" in debug_values:
                    top1_length_values.append(debug_values["Len"])
                if "Whole" in debug_values:
                    top1_whole_values.append(debug_values["Whole"])

        latest_by_key[record_key] = record
        history_by_key.setdefault(record_key, []).append(top1_text)
        if top1_text:
            top1_counter[top1_text] += 1

    input_summaries: dict[str, dict[str, Any]] = {}
    unstable_inputs: list[dict[str, Any]] = []
    top1_hits = 0
    top3_hits = 0
    evaluated_inputs = 0

    for record_key, record in latest_by_key.items():
        input_text = str(record.get("input", ""))
        preceding_text = str(record.get("preceding_text", ""))
        candidates = record.get("candidates") or []
        if not isinstance(candidates, list):
            candidates = []
        candidate_texts = [
            str(candidate.get("text", ""))
            for candidate in candidates
            if isinstance(candidate, dict)
        ]
        top1_text = candidate_texts[0] if candidate_texts else ""
        history = [text for text in history_by_key.get(record_key, []) if text]
        unique_top1 = sorted(set(history))
        if len(unique_top1) > 1:
            unstable_inputs.append(
                {
                    "input": input_text,
                    "preceding_text": preceding_text,
                    "top1_variants": unique_top1,
                    "occurrence_count": len(history),
                }
            )

        expected_text = reference_by_composite_key.get(record_key)
        if expected_text is None:
            expected_text = reference_by_input.get(input_text)
        top1_match = None
        top3_match = None
        if expected_text:
            evaluated_inputs += 1
            top1_match = top1_text == expected_text
            top3_match = expected_text in candidate_texts[:3]
            if top1_match:
                top1_hits += 1
            if top3_match:
                top3_hits += 1

        input_summaries[record_key] = {
            "input": input_text,
            "preceding_text": preceding_text,
            "expected_text": expected_text,
            "top1_text": top1_text,
            "top3_texts": candidate_texts[:3],
            "candidate_count": int(record.get("candidate_count", len(candidate_texts)) or 0),
            "top1_match": top1_match,
            "top3_match": top3_match,
            "timestamp_ms": record.get("timestamp_ms"),
            "candidates": candidates,
        }

    metrics = {
        "snapshot_format": "jsonl",
        "total_records": len(records),
        "unique_inputs": len(latest_by_key),
        "records_with_candidates": records_with_candidates,
        "records_without_candidates": len(records) - records_with_candidates,
        "records_with_debug": records_with_debug,
        "avg_candidate_count": round_or_none(safe_mean(candidate_counts)),
        "avg_top1_total": round_or_none(safe_mean(top1_total_values)),
        "avg_top1_dict": round_or_none(safe_mean(top1_dict_values)),
        "avg_top1_dict_norm": round_or_none(safe_mean(top1_dict_norm_values)),
        "avg_top1_lm_scaled": round_or_none(safe_mean(top1_lm_scaled_values)),
        "avg_top1_lm_avg": round_or_none(safe_mean(top1_lm_avg_values)),
        "avg_top1_boundary": round_or_none(safe_mean(top1_boundary_values)),
        "avg_top1_oov": round_or_none(safe_mean(top1_oov_values)),
        "avg_top1_len": round_or_none(safe_mean(top1_length_values)),
        "avg_top1_whole": round_or_none(safe_mean(top1_whole_values)),
        "unstable_input_count": len(unstable_inputs),
        "reference_case_count": len(reference_by_composite_key) or len(reference_by_input),
        "evaluated_input_count": evaluated_inputs,
        "top1_accuracy": round_or_none(top1_hits / evaluated_inputs) if evaluated_inputs else None,
        "top3_accuracy": round_or_none(top3_hits / evaluated_inputs) if evaluated_inputs else None,
        "most_common_top1_texts": [
            {"text": text, "count": count}
            for text, count in top1_counter.most_common(20)
        ],
    }
    unstable_inputs.sort(
        key=lambda item: (-item["occurrence_count"], item["input"], item["preceding_text"])
    )
    return metrics, input_summaries, unstable_inputs


def write_json(path: Path, payload: Any) -> None:
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
        handle.write("\n")


def write_report(
    path: Path,
    metrics: dict[str, Any],
    input_summaries: dict[str, dict[str, Any]],
    unstable_inputs: list[dict[str, Any]],
) -> None:
    empty_candidate_inputs = [
        summary["input"]
        for summary in input_summaries.values()
        if summary["candidate_count"] <= 0
    ]
    mismatched_inputs = [
        summary
        for summary in input_summaries.values()
        if summary["top1_match"] is False
    ]

    lines = [
        "# Local Snapshot Summary",
        "",
        "## Overview",
        "",
        f"- Total records: {metrics['total_records']}",
        f"- Unique inputs: {metrics['unique_inputs']}",
        f"- Records with candidates: {metrics['records_with_candidates']}",
        f"- Records without candidates: {metrics['records_without_candidates']}",
        f"- Records with parsed debug info: {metrics['records_with_debug']}",
        f"- Unstable inputs: {metrics['unstable_input_count']}",
        "",
        "## Score Averages",
        "",
        f"- Average candidate count: {metrics['avg_candidate_count']}",
        f"- Average Top-1 Total: {metrics['avg_top1_total']}",
        f"- Average Top-1 Dict: {metrics['avg_top1_dict']}",
        f"- Average Top-1 DictNorm: {metrics['avg_top1_dict_norm']}",
        f"- Average Top-1 LmScaled: {metrics['avg_top1_lm_scaled']}",
        f"- Average Top-1 LmAvg: {metrics['avg_top1_lm_avg']}",
        f"- Average Top-1 Boundary: {metrics['avg_top1_boundary']}",
        f"- Average Top-1 OOV: {metrics['avg_top1_oov']}",
        f"- Average Top-1 Len: {metrics['avg_top1_len']}",
        f"- Average Top-1 Whole: {metrics['avg_top1_whole']}",
        "",
    ]

    if metrics["evaluated_input_count"]:
        lines.extend(
            [
                "## Reference Metrics",
                "",
                f"- Evaluated inputs: {metrics['evaluated_input_count']}",
                f"- Top-1 accuracy: {metrics['top1_accuracy']}",
                f"- Top-3 accuracy: {metrics['top3_accuracy']}",
                "",
            ]
        )

    lines.extend(["## Frequent Top-1 Outputs", ""])
    common_top1 = metrics.get("most_common_top1_texts", [])
    if common_top1:
        for item in common_top1[:10]:
            lines.append(f"- {item['text']} ({item['count']})")
    else:
        lines.append("- None")
    lines.append("")

    lines.extend(["## Empty Candidate Inputs", ""])
    if empty_candidate_inputs:
        for input_text in empty_candidate_inputs[:20]:
            lines.append(f"- {input_text}")
    else:
        lines.append("- None")
    lines.append("")

    lines.extend(["## Unstable Inputs", ""])
    if unstable_inputs:
        for item in unstable_inputs[:20]:
            variants = " | ".join(item["top1_variants"])
            lines.append(
                f"- {item['input']} -> {variants} (records={item['occurrence_count']})"
            )
    else:
        lines.append("- None")
    lines.append("")

    if mismatched_inputs:
        lines.extend(["## Top-1 Mismatches", ""])
        for item in mismatched_inputs[:20]:
            lines.append(
                f"- {item['input']} -> top1={item['top1_text']} | expected={item['expected_text']}"
            )
        lines.append("")

    with path.open("w", encoding="utf-8") as handle:
        handle.write("\n".join(lines).rstrip() + "\n")


def main() -> int:
    args = parse_args()
    snapshot_path = Path(args.snapshot).expanduser().resolve()
    if not snapshot_path.is_file():
        raise SystemExit(f"Snapshot file not found: {snapshot_path}")

    output_dir = (
        Path(args.output_dir).expanduser().resolve()
        if args.output_dir
        else snapshot_path.parent / "snapshot_summary"
    )
    output_dir.mkdir(parents=True, exist_ok=True)

    records = load_jsonl(snapshot_path)
    reference_by_composite_key, reference_by_input = load_reference_map(
        Path(args.reference).expanduser().resolve() if args.reference else None
    )
    metrics, input_summaries, unstable_inputs = build_summary(
        records,
        reference_by_composite_key,
        reference_by_input,
    )

    write_json(output_dir / "metrics.json", metrics)
    write_json(output_dir / "latest_candidates.json", input_summaries)
    write_report(
        output_dir / "regression_report.md",
        metrics,
        input_summaries,
        unstable_inputs,
    )
    print(f"Summary written to: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
