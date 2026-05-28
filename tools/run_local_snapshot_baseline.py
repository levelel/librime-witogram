#!/usr/bin/env python3
"""Drive Rime local snapshot generation from the full corpus."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from pypinyin import Style, lazy_pinyin


CHUNK_SEPARATOR_RE = re.compile(r"[\r\n\s]+")
SUPPORTED_PUNCTUATION = {
    "，": ",",
    "。": ".",
    "！": "!",
    "？": "?",
    "；": ";",
    "：": ":",
    "（": "(",
    "）": ")",
    "【": "[",
    "】": "]",
    "《": "<",
    "》": ">",
    "“": '"',
    "”": '"',
    "‘": "'",
    "’": "'",
    "、": "\\",
    "…": "^",
    "—": "_",
    "+": "+",
    "-": "-",
    ",": ",",
    ".": ".",
    "!": "!",
    "?": "?",
    ";": ";",
    ":": ":",
    "(": "(",
    ")": ")",
    "[": "[",
    "]": "]",
    "<": "<",
    ">": ">",
    '"': '"',
    "'": "'",
}


@dataclass
class Step:
    kind: str
    expected_text: str
    raw_input: str
    source_index: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the full local snapshot baseline with rime_api_console."
    )
    parser.add_argument(
        "--corpus",
        default=r"c:\Code\outwit\witset-worker\tests\test_sentence.txt",
        help="Path to the corpus text file.",
    )
    parser.add_argument(
        "--console-exe",
        default=(
            r"c:\Code\outwit\outwit-windows\librime\build_x64\bin\Release"
            r"\rime_api_console.exe"
        ),
        help="Path to rime_api_console.exe.",
    )
    parser.add_argument(
        "--console-cwd",
        default=r"c:\Users\Bing\AppData\Roaming\witty",
        help="Working directory for rime_api_console.",
    )
    parser.add_argument(
        "--runtime-bin-dir",
        default=r"c:\Code\outwit\outwit-windows\librime\dist_x64\bin",
        help="Directory added to PATH before launching the console.",
    )
    parser.add_argument(
        "--snapshot",
        default=r"c:\Users\Bing\AppData\Roaming\witty\debug\witset_local_snapshot.jsonl",
        help="Path to the snapshot JSONL file.",
    )
    parser.add_argument(
        "--summary-dir",
        default=r"c:\Users\Bing\AppData\Roaming\witty\debug\snapshot_summary",
        help="Output directory for reference cases and summary files.",
    )
    parser.add_argument(
        "--schema",
        default="witset",
        help="Schema id used for the run.",
    )
    parser.add_argument(
        "--max-input-length",
        type=int,
        default=72,
        help="Maximum raw input length for a single text chunk.",
    )
    parser.add_argument(
        "--snapshot-timeout-seconds",
        type=float,
        default=10.0,
        help="Maximum time to wait for each snapshot record.",
    )
    parser.add_argument(
        "--limit",
        type=int,
        help="Optional limit for text steps, useful for smoke tests.",
    )
    parser.add_argument(
        "--keep-existing-snapshot",
        action="store_true",
        help="Do not delete the previous snapshot file before running.",
    )
    parser.add_argument(
        "--persist-every-text-steps",
        type=int,
        default=10,
        help="Persist metadata every N completed text steps.",
    )
    parser.add_argument(
        "--skip-summary",
        action="store_true",
        help="Skip running summarize_local_snapshot.py after the baseline finishes.",
    )
    return parser.parse_args()


def is_cjk(char: str) -> bool:
    code = ord(char)
    return (
        0x3400 <= code <= 0x4DBF
        or 0x4E00 <= code <= 0x9FFF
        or 0xF900 <= code <= 0xFAFF
        or 0x20000 <= code <= 0x2EBEF
    )


def is_ascii_word_char(char: str) -> bool:
    return char.isascii() and char.isalnum()


def to_full_pinyin(text: str) -> str:
    syllables = lazy_pinyin(text, style=Style.NORMAL, errors="default", strict=False)
    return "".join(syllables).lower()


def append_chunk(steps: list[Step], text: str, source_index: int) -> None:
    if not text:
        return
    if any(is_cjk(char) for char in text):
        raw_input = to_full_pinyin(text)
        if raw_input:
            steps.append(
                Step(
                    kind="text",
                    expected_text=text,
                    raw_input=raw_input,
                    source_index=source_index,
                )
            )
        return
    if any(is_ascii_word_char(char) for char in text):
        steps.append(
            Step(
                kind="ascii",
                expected_text=text,
                raw_input=text,
                source_index=source_index,
            )
        )


def flush_buffer(
    steps: list[Step],
    buffer: list[str],
    buffer_kind: str | None,
    chunk_source_index: int,
) -> tuple[list[str], str | None, int]:
    if buffer:
        append_chunk(steps, "".join(buffer), chunk_source_index)
    return [], None, -1


def build_steps(text: str, max_input_length: int) -> list[Step]:
    steps: list[Step] = []
    buffer: list[str] = []
    buffer_kind: str | None = None
    chunk_source_index = -1

    for source_index, char in enumerate(text):
        if CHUNK_SEPARATOR_RE.match(char):
            buffer, buffer_kind, chunk_source_index = flush_buffer(
                steps, buffer, buffer_kind, chunk_source_index
            )
            continue

        if char in SUPPORTED_PUNCTUATION:
            buffer, buffer_kind, chunk_source_index = flush_buffer(
                steps, buffer, buffer_kind, chunk_source_index
            )
            steps.append(
                Step(
                    kind="punctuation",
                    expected_text=char,
                    raw_input=SUPPORTED_PUNCTUATION[char],
                    source_index=source_index,
                )
            )
            continue

        kind = "text" if is_cjk(char) else "ascii" if is_ascii_word_char(char) else None
        if kind is None:
            buffer, buffer_kind, chunk_source_index = flush_buffer(
                steps, buffer, buffer_kind, chunk_source_index
            )
            continue

        if not buffer:
            buffer_kind = kind
            chunk_source_index = source_index

        candidate_chunk = "".join(buffer + [char])
        candidate_input = (
            to_full_pinyin(candidate_chunk) if kind == "text" else candidate_chunk
        )
        if buffer_kind != kind or len(candidate_input) > max_input_length:
            buffer, buffer_kind, chunk_source_index = flush_buffer(
                steps, buffer, buffer_kind, chunk_source_index
            )
            buffer_kind = kind
            chunk_source_index = source_index

        buffer.append(char)

    flush_buffer(steps, buffer, buffer_kind, chunk_source_index)
    return steps


def build_env(runtime_bin_dir: Path) -> dict[str, str]:
    env = os.environ.copy()
    env["PATH"] = str(runtime_bin_dir) + os.pathsep + env.get("PATH", "")
    return env


def launch_console(
    console_exe: Path,
    console_cwd: Path,
    runtime_bin_dir: Path,
) -> subprocess.Popen[str]:
    return subprocess.Popen(
        [str(console_exe)],
        cwd=str(console_cwd),
        env=build_env(runtime_bin_dir),
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def send_command(process: subprocess.Popen[str], command: str) -> None:
    if process.stdin is None:
        raise RuntimeError("Console stdin is not available.")
    process.stdin.write(command + "\n")
    process.stdin.flush()


def parse_snapshot_line(raw_line: bytes) -> dict[str, Any] | None:
    line = raw_line.strip()
    if not line:
        return None
    candidates: list[bytes] = [line]
    json_start = line.find(b"{")
    if json_start > 0:
        candidates.append(line[json_start:])
    for candidate in candidates:
        try:
            data = json.loads(candidate.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
        if isinstance(data, dict):
            return data
    return None


class SnapshotTailReader:
    def __init__(self, snapshot_path: Path) -> None:
        self.snapshot_path = snapshot_path
        self._handle: Any | None = None
        self._offset = 0

    def _ensure_open(self) -> bool:
        if not self.snapshot_path.exists():
            return False
        if self._handle is None or self._handle.closed:
            self._handle = self.snapshot_path.open("rb")
            self._handle.seek(self._offset)
        return True

    def read_new_records(self) -> list[dict[str, Any]]:
        if not self._ensure_open():
            return []
        current_size = self.snapshot_path.stat().st_size
        if current_size < self._offset:
            self.close()
            self._offset = 0
            if not self._ensure_open():
                return []
        assert self._handle is not None
        self._handle.seek(self._offset)
        payload = self._handle.read()
        if not payload:
            return []
        if not payload.endswith(b"\n"):
            last_newline = payload.rfind(b"\n")
            if last_newline < 0:
                return []
            payload = payload[: last_newline + 1]
        self._offset += len(payload)
        records: list[dict[str, Any]] = []
        for raw_line in payload.splitlines():
            if not raw_line.strip():
                continue
            data = parse_snapshot_line(raw_line)
            if data is not None:
                records.append(data)
        return records

    def close(self) -> None:
        if self._handle is not None and not self._handle.closed:
            self._handle.close()
        self._handle = None


def wait_for_snapshot_record(
    snapshot_reader: SnapshotTailReader,
    timeout_seconds: float,
    expected_input: str,
    expected_preceding_text: str,
) -> dict[str, Any]:
    startup_timeout_seconds = max(timeout_seconds, 5.0 + len(expected_input) * 0.5)
    idle_timeout_seconds = max(timeout_seconds, 2.0 + len(expected_input) * 0.3)
    start_time = time.monotonic()
    last_progress_time = start_time
    hard_deadline = start_time + max(startup_timeout_seconds + idle_timeout_seconds * 4.0, 60.0)
    last_seen_input = ""
    poll_sleep_seconds = 0.02
    while time.monotonic() < hard_deadline:
        records = snapshot_reader.read_new_records()
        for record in records:
            record_input = str(record.get("input", ""))
            if record_input == expected_input:
                return record
            if expected_input.startswith(record_input):
                last_progress_time = time.monotonic()
                last_seen_input = record_input
                poll_sleep_seconds = 0.02
        if records:
            poll_sleep_seconds = 0.02
        now = time.monotonic()
        if not last_seen_input and now - start_time >= startup_timeout_seconds:
            break
        if last_seen_input and now - last_progress_time >= idle_timeout_seconds:
            break
        time.sleep(poll_sleep_seconds)
        poll_sleep_seconds = min(poll_sleep_seconds * 1.5, 0.10)
    raise TimeoutError(
        "Timed out waiting for snapshot record: "
        f"{expected_input} (startup_timeout_seconds={startup_timeout_seconds:.2f}, "
        f"idle_timeout_seconds={idle_timeout_seconds:.2f}, "
        f"last_seen_input={last_seen_input!r})"
    )


def choose_rank(record: dict[str, Any], expected_text: str) -> tuple[int, int | None]:
    candidates = record.get("candidates") or []
    if not isinstance(candidates, list) or not candidates:
        return 1, None
    expected_rank: int | None = None
    for candidate in candidates:
        if not isinstance(candidate, dict):
            continue
        rank = int(candidate.get("rank", 0) or 0)
        text = str(candidate.get("text", ""))
        if text == expected_text:
            expected_rank = rank
            break
    return expected_rank or 1, expected_rank


def write_json(path: Path, payload: Any) -> None:
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
        handle.write("\n")


def write_jsonl(path: Path, records: list[dict[str, Any]]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record, ensure_ascii=False) + "\n")


def initialize_jsonl(path: Path) -> None:
    path.write_text("", encoding="utf-8")


def append_jsonl_record(path: Path, record: dict[str, Any]) -> None:
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(record, ensure_ascii=False) + "\n")


def persist_run_state(
    metadata_path: Path,
    run_metadata: dict[str, Any],
) -> None:
    write_json(metadata_path, run_metadata)


def run_summary(snapshot_path: Path, summary_dir: Path, reference_path: Path) -> None:
    script_path = Path(__file__).with_name("summarize_local_snapshot.py")
    subprocess.run(
        [
            sys.executable,
            str(script_path),
            "--snapshot",
            str(snapshot_path),
            "--output-dir",
            str(summary_dir),
            "--reference",
            str(reference_path),
        ],
        check=True,
    )


def main() -> int:
    args = parse_args()
    corpus_path = Path(args.corpus).expanduser().resolve()
    console_exe = Path(args.console_exe).expanduser().resolve()
    console_cwd = Path(args.console_cwd).expanduser().resolve()
    runtime_bin_dir = Path(args.runtime_bin_dir).expanduser().resolve()
    snapshot_path = Path(args.snapshot).expanduser().resolve()
    summary_dir = Path(args.summary_dir).expanduser().resolve()
    reference_path = summary_dir / "reference_cases.jsonl"
    metadata_path = summary_dir / "baseline_run_metadata.json"

    if not corpus_path.is_file():
        raise SystemExit(f"Corpus file not found: {corpus_path}")
    if not console_exe.is_file():
        raise SystemExit(f"Console executable not found: {console_exe}")
    if not console_cwd.is_dir():
        raise SystemExit(f"Console working directory not found: {console_cwd}")
    if not runtime_bin_dir.is_dir():
        raise SystemExit(f"Runtime bin directory not found: {runtime_bin_dir}")

    summary_dir.mkdir(parents=True, exist_ok=True)
    snapshot_path.parent.mkdir(parents=True, exist_ok=True)
    if snapshot_path.exists() and not args.keep_existing_snapshot:
        snapshot_path.unlink()
    snapshot_path.touch(exist_ok=True)

    corpus_text = corpus_path.read_text(encoding="utf-8")
    all_steps = build_steps(corpus_text, args.max_input_length)
    text_steps = [step for step in all_steps if step.kind == "text"]
    if args.limit is not None:
        allowed_text_indices = {step.source_index for step in text_steps[: args.limit]}
        all_steps = [
            step
            for step in all_steps
            if step.kind != "text" or step.source_index in allowed_text_indices
        ]
        text_steps = [step for step in all_steps if step.kind == "text"]

    process = launch_console(console_exe, console_cwd, runtime_bin_dir)
    reference_records: list[dict[str, Any]] = []
    run_metadata: dict[str, Any] = {
        "corpus": str(corpus_path),
        "console_exe": str(console_exe),
        "snapshot": str(snapshot_path),
        "summary_dir": str(summary_dir),
        "schema": args.schema,
        "max_input_length": args.max_input_length,
        "step_count": len(all_steps),
        "text_step_count": len(text_steps),
        "completed_text_steps": 0,
        "expected_not_found_count": 0,
        "preceding_text_mismatch_count": 0,
        "status": "running",
    }

    if args.persist_every_text_steps < 1:
        raise SystemExit("--persist-every-text-steps must be >= 1")

    snapshot_reader = SnapshotTailReader(snapshot_path)
    current_preceding_text = ""
    run_completed = False
    run_start_time = time.monotonic()
    initialize_jsonl(reference_path)
    persist_run_state(metadata_path, run_metadata)
    try:
        send_command(process, f"select schema {args.schema}")
        send_command(process, "set option !llm_level_3")
        send_command(process, "set option !llm_level_2")
        send_command(process, "set option llm_level_1")

        for step_index, step in enumerate(all_steps, start=1):
            if step.kind == "text":
                if current_preceding_text:
                    send_command(process, f"set preceding text {current_preceding_text}")
                else:
                    send_command(process, "clear preceding text")
                send_command(process, step.raw_input)
                record = wait_for_snapshot_record(
                    snapshot_reader=snapshot_reader,
                    timeout_seconds=args.snapshot_timeout_seconds,
                    expected_input=step.raw_input,
                    expected_preceding_text=current_preceding_text,
                )
                snapshot_preceding_text = str(record.get("preceding_text", ""))
                if snapshot_preceding_text != current_preceding_text:
                    run_metadata["preceding_text_mismatch_count"] += 1
                selected_rank, expected_rank = choose_rank(record, step.expected_text)
                if expected_rank is None:
                    run_metadata["expected_not_found_count"] += 1
                reference_records.append(
                    {
                        "case_id": f"case_{len(reference_records) + 1:06d}",
                        "input": step.raw_input,
                        "preceding_text": current_preceding_text,
                        "snapshot_preceding_text": snapshot_preceding_text,
                        "expected_text": step.expected_text,
                        "selected_rank": None,
                        "expected_rank": expected_rank,
                        "source_index": step.source_index,
                        "step_index": step_index,
                    }
                )
                append_jsonl_record(reference_path, reference_records[-1])
                send_command(process, "clear composition")
                run_metadata["completed_text_steps"] += 1
                run_metadata["last_completed_case_id"] = reference_records[-1]["case_id"]
                run_metadata["last_completed_step_index"] = step_index
                if (
                    run_metadata["completed_text_steps"] % args.persist_every_text_steps
                    == 0
                ):
                    persist_run_state(metadata_path, run_metadata)
                if run_metadata["completed_text_steps"] % 50 == 0:
                    print(
                        f"Processed {run_metadata['completed_text_steps']}/"
                        f"{len(text_steps)} text steps..."
                    )
            current_preceding_text += step.expected_text
        run_completed = True
    finally:
        snapshot_reader.close()
        try:
            send_command(process, "exit")
        except Exception:
            pass
        if process.stdin is not None:
            process.stdin.close()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        run_metadata["status"] = "completed" if run_completed else "interrupted"
        run_metadata["wall_time_seconds"] = round(time.monotonic() - run_start_time, 3)
        persist_run_state(metadata_path, run_metadata)
    if not args.skip_summary:
        run_summary(snapshot_path, summary_dir, reference_path)
    print(f"Reference cases written to: {reference_path}")
    print(f"Run metadata written to: {metadata_path}")
    print(f"Summary directory: {summary_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
