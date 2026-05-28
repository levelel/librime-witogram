#!/usr/bin/env python3
"""Run a stock Rime + octagram benchmark against the shared corpus."""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

from run_local_snapshot_baseline import (
    build_steps,
    choose_rank,
    run_summary,
    write_json,
    write_jsonl,
)


XK_PAGE_DOWN = 0xFF56


RimeSessionId = ctypes.c_uint64
Bool = ctypes.c_int


class RimeTraits(ctypes.Structure):
    _fields_ = [
        ("data_size", ctypes.c_int),
        ("shared_data_dir", ctypes.c_char_p),
        ("user_data_dir", ctypes.c_char_p),
        ("distribution_name", ctypes.c_char_p),
        ("distribution_code_name", ctypes.c_char_p),
        ("distribution_version", ctypes.c_char_p),
        ("app_name", ctypes.c_char_p),
        ("modules", ctypes.POINTER(ctypes.c_char_p)),
        ("min_log_level", ctypes.c_int),
        ("log_dir", ctypes.c_char_p),
        ("prebuilt_data_dir", ctypes.c_char_p),
        ("staging_dir", ctypes.c_char_p),
    ]


class RimeComposition(ctypes.Structure):
    _fields_ = [
        ("length", ctypes.c_int),
        ("cursor_pos", ctypes.c_int),
        ("sel_start", ctypes.c_int),
        ("sel_end", ctypes.c_int),
        ("preedit", ctypes.c_char_p),
    ]


class RimeCandidate(ctypes.Structure):
    _fields_ = [
        ("text", ctypes.c_char_p),
        ("comment", ctypes.c_char_p),
        ("reserved", ctypes.c_void_p),
    ]


class RimeMenu(ctypes.Structure):
    _fields_ = [
        ("page_size", ctypes.c_int),
        ("page_no", ctypes.c_int),
        ("is_last_page", Bool),
        ("highlighted_candidate_index", ctypes.c_int),
        ("num_candidates", ctypes.c_int),
        ("candidates", ctypes.POINTER(RimeCandidate)),
        ("select_keys", ctypes.c_char_p),
    ]


class RimeContext(ctypes.Structure):
    _fields_ = [
        ("data_size", ctypes.c_int),
        ("composition", RimeComposition),
        ("menu", RimeMenu),
        ("commit_text_preview", ctypes.c_char_p),
        ("select_labels", ctypes.POINTER(ctypes.c_char_p)),
    ]


class RimeCommit(ctypes.Structure):
    _fields_ = [
        ("data_size", ctypes.c_int),
        ("text", ctypes.c_char_p),
    ]


class RimeStatus(ctypes.Structure):
    _fields_ = [
        ("data_size", ctypes.c_int),
        ("schema_id", ctypes.c_char_p),
        ("schema_name", ctypes.c_char_p),
        ("is_disabled", Bool),
        ("is_composing", Bool),
        ("is_ascii_mode", Bool),
        ("is_full_shape", Bool),
        ("is_simplified", Bool),
        ("is_traditional", Bool),
        ("is_ascii_punct", Bool),
    ]


class RimeApi(ctypes.Structure):
    _fields_ = [
        ("data_size", ctypes.c_int),
        ("setup", ctypes.c_void_p),
        ("set_notification_handler", ctypes.c_void_p),
        ("initialize", ctypes.c_void_p),
        ("finalize", ctypes.c_void_p),
        ("start_maintenance", ctypes.c_void_p),
        ("is_maintenance_mode", ctypes.c_void_p),
        ("join_maintenance_thread", ctypes.c_void_p),
        ("deployer_initialize", ctypes.c_void_p),
        ("prebuild", ctypes.c_void_p),
        ("deploy", ctypes.c_void_p),
        ("deploy_schema", ctypes.c_void_p),
        ("deploy_config_file", ctypes.c_void_p),
        ("sync_user_data", ctypes.c_void_p),
        ("create_session", ctypes.c_void_p),
        ("find_session", ctypes.c_void_p),
        ("destroy_session", ctypes.c_void_p),
        ("cleanup_stale_sessions", ctypes.c_void_p),
        ("cleanup_all_sessions", ctypes.c_void_p),
        ("process_key", ctypes.c_void_p),
        ("commit_composition", ctypes.c_void_p),
        ("clear_composition", ctypes.c_void_p),
        ("get_commit", ctypes.c_void_p),
        ("free_commit", ctypes.c_void_p),
        ("get_context", ctypes.c_void_p),
        ("free_context", ctypes.c_void_p),
        ("get_status", ctypes.c_void_p),
        ("free_status", ctypes.c_void_p),
        ("set_option", ctypes.c_void_p),
        ("get_option", ctypes.c_void_p),
        ("set_property", ctypes.c_void_p),
        ("get_property", ctypes.c_void_p),
        ("get_schema_list", ctypes.c_void_p),
        ("free_schema_list", ctypes.c_void_p),
        ("get_current_schema", ctypes.c_void_p),
        ("select_schema", ctypes.c_void_p),
        ("schema_open", ctypes.c_void_p),
        ("config_open", ctypes.c_void_p),
        ("config_close", ctypes.c_void_p),
        ("config_get_bool", ctypes.c_void_p),
        ("config_get_int", ctypes.c_void_p),
        ("config_get_double", ctypes.c_void_p),
        ("config_get_string", ctypes.c_void_p),
        ("config_get_cstring", ctypes.c_void_p),
        ("config_update_signature", ctypes.c_void_p),
        ("config_begin_map", ctypes.c_void_p),
        ("config_next", ctypes.c_void_p),
        ("config_end", ctypes.c_void_p),
        ("simulate_key_sequence", ctypes.c_void_p),
        ("register_module", ctypes.c_void_p),
        ("find_module", ctypes.c_void_p),
        ("run_task", ctypes.c_void_p),
        ("get_shared_data_dir", ctypes.c_void_p),
        ("get_user_data_dir", ctypes.c_void_p),
        ("get_sync_dir", ctypes.c_void_p),
        ("get_user_id", ctypes.c_void_p),
        ("get_user_data_sync_dir", ctypes.c_void_p),
        ("config_init", ctypes.c_void_p),
        ("config_load_string", ctypes.c_void_p),
        ("config_set_bool", ctypes.c_void_p),
        ("config_set_int", ctypes.c_void_p),
        ("config_set_double", ctypes.c_void_p),
        ("config_set_string", ctypes.c_void_p),
        ("config_get_item", ctypes.c_void_p),
        ("config_set_item", ctypes.c_void_p),
        ("config_clear", ctypes.c_void_p),
        ("config_create_list", ctypes.c_void_p),
        ("config_create_map", ctypes.c_void_p),
        ("config_list_size", ctypes.c_void_p),
        ("config_begin_list", ctypes.c_void_p),
        ("get_input", ctypes.c_void_p),
        ("get_caret_pos", ctypes.c_void_p),
        ("select_candidate", ctypes.c_void_p),
        ("get_version", ctypes.c_void_p),
        ("set_caret_pos", ctypes.c_void_p),
        ("select_candidate_on_current_page", ctypes.c_void_p),
        ("candidate_list_begin", ctypes.c_void_p),
        ("candidate_list_next", ctypes.c_void_p),
        ("candidate_list_end", ctypes.c_void_p),
        ("user_config_open", ctypes.c_void_p),
        ("candidate_list_from_index", ctypes.c_void_p),
        ("get_prebuilt_data_dir", ctypes.c_void_p),
        ("get_staging_dir", ctypes.c_void_p),
        ("commit_proto", ctypes.c_void_p),
        ("context_proto", ctypes.c_void_p),
        ("status_proto", ctypes.c_void_p),
        ("get_state_label", ctypes.c_void_p),
        ("delete_candidate", ctypes.c_void_p),
        ("delete_candidate_on_current_page", ctypes.c_void_p),
        ("get_state_label_abbreviated", ctypes.c_void_p),
        ("set_input", ctypes.c_void_p),
        ("get_shared_data_dir_s", ctypes.c_void_p),
        ("get_user_data_dir_s", ctypes.c_void_p),
        ("get_prebuilt_data_dir_s", ctypes.c_void_p),
        ("get_staging_dir_s", ctypes.c_void_p),
        ("get_sync_dir_s", ctypes.c_void_p),
        ("highlight_candidate", ctypes.c_void_p),
        ("highlight_candidate_on_current_page", ctypes.c_void_p),
        ("change_page", ctypes.c_void_p),
        ("set_external_preceding_text", ctypes.c_void_p),
        ("set_external_following_text", ctypes.c_void_p),
        ("get_external_preceding_text", ctypes.c_void_p),
        ("get_external_following_text", ctypes.c_void_p),
        ("clear_external_context", ctypes.c_void_p),
    ]


def init_self_versioned(struct_obj: ctypes.Structure) -> None:
    struct_obj.data_size = ctypes.sizeof(struct_obj.__class__) - ctypes.sizeof(ctypes.c_int)


def decode_cstring(value: bytes | None) -> str:
    if not value:
        return ""
    return value.decode("utf-8", errors="replace")


@dataclass
class CandidateSnapshot:
    text: str
    comment: str
    rank: int


class StockRimeApi:
    def __init__(self, dll_path: Path) -> None:
        if not dll_path.is_file():
            raise FileNotFoundError(f"Rime DLL not found: {dll_path}")
        self.dll = ctypes.CDLL(str(dll_path))
        self._bind_direct_exports()
        self.api = self._load_api_struct()
        self.set_external_preceding_text_func = self._bind_api_function(
            "set_external_preceding_text", Bool, RimeSessionId, ctypes.c_char_p
        )
        self.clear_external_context_func = self._bind_api_function(
            "clear_external_context", Bool, RimeSessionId
        )
        self.get_version_func = self._bind_api_function("get_version", ctypes.c_char_p)

    def _bind_direct_exports(self) -> None:
        self.dll.RimeSetup.argtypes = [ctypes.POINTER(RimeTraits)]
        self.dll.RimeSetup.restype = None
        self.dll.RimeInitialize.argtypes = [ctypes.POINTER(RimeTraits)]
        self.dll.RimeInitialize.restype = None
        self.dll.RimeFinalize.argtypes = []
        self.dll.RimeFinalize.restype = None
        self.dll.RimeStartMaintenance.argtypes = [Bool]
        self.dll.RimeStartMaintenance.restype = Bool
        self.dll.RimeJoinMaintenanceThread.argtypes = []
        self.dll.RimeJoinMaintenanceThread.restype = None
        self.dll.RimeCreateSession.argtypes = []
        self.dll.RimeCreateSession.restype = RimeSessionId
        self.dll.RimeDestroySession.argtypes = [RimeSessionId]
        self.dll.RimeDestroySession.restype = Bool
        self.dll.RimeProcessKey.argtypes = [RimeSessionId, ctypes.c_int, ctypes.c_int]
        self.dll.RimeProcessKey.restype = Bool
        self.dll.RimeCommitComposition.argtypes = [RimeSessionId]
        self.dll.RimeCommitComposition.restype = Bool
        self.dll.RimeClearComposition.argtypes = [RimeSessionId]
        self.dll.RimeClearComposition.restype = None
        self.dll.RimeGetCommit.argtypes = [RimeSessionId, ctypes.POINTER(RimeCommit)]
        self.dll.RimeGetCommit.restype = Bool
        self.dll.RimeFreeCommit.argtypes = [ctypes.POINTER(RimeCommit)]
        self.dll.RimeFreeCommit.restype = Bool
        self.dll.RimeGetContext.argtypes = [RimeSessionId, ctypes.POINTER(RimeContext)]
        self.dll.RimeGetContext.restype = Bool
        self.dll.RimeFreeContext.argtypes = [ctypes.POINTER(RimeContext)]
        self.dll.RimeFreeContext.restype = Bool
        self.dll.RimeGetStatus.argtypes = [RimeSessionId, ctypes.POINTER(RimeStatus)]
        self.dll.RimeGetStatus.restype = Bool
        self.dll.RimeFreeStatus.argtypes = [ctypes.POINTER(RimeStatus)]
        self.dll.RimeFreeStatus.restype = Bool
        self.dll.RimeSelectSchema.argtypes = [RimeSessionId, ctypes.c_char_p]
        self.dll.RimeSelectSchema.restype = Bool
        self.dll.RimeSetOption.argtypes = [RimeSessionId, ctypes.c_char_p, Bool]
        self.dll.RimeSetOption.restype = None

    def _load_api_struct(self) -> RimeApi:
        self.dll.rime_get_api.argtypes = []
        self.dll.rime_get_api.restype = ctypes.POINTER(RimeApi)
        api_ptr = self.dll.rime_get_api()
        if not api_ptr:
            raise RuntimeError("Failed to obtain RimeApi from rime.dll.")
        return api_ptr.contents

    def _bind_api_function(
        self, name: str, restype: Any, *argtypes: Any
    ) -> Callable[..., Any] | None:
        address = getattr(self.api, name, None)
        if not address:
            return None
        return ctypes.CFUNCTYPE(restype, *argtypes)(address)

    def version(self) -> str:
        if self.get_version_func is None:
            return ""
        return decode_cstring(self.get_version_func())


class StockRimeSession:
    def __init__(
        self,
        api: StockRimeApi,
        shared_data_dir: Path,
        user_data_dir: Path,
        *,
        app_name: str,
        deploy: bool,
    ) -> None:
        self.api = api
        self.shared_data_dir = shared_data_dir
        self.user_data_dir = user_data_dir
        self.app_name = app_name
        self.deploy = deploy
        self._traits = self._build_traits()

    def _build_traits(self) -> RimeTraits:
        traits = RimeTraits()
        init_self_versioned(traits)
        shared_build = self.shared_data_dir / "build"
        user_build = self.user_data_dir / "build"
        traits.shared_data_dir = str(self.shared_data_dir).encode("utf-8")
        traits.user_data_dir = str(self.user_data_dir).encode("utf-8")
        traits.distribution_name = b"Weasel"
        traits.distribution_code_name = b"weasel"
        traits.distribution_version = b"0.17.4"
        traits.app_name = self.app_name.encode("utf-8")
        traits.modules = None
        traits.min_log_level = 0
        traits.log_dir = str(self.user_data_dir / "logs").encode("utf-8")
        traits.prebuilt_data_dir = str(shared_build).encode("utf-8")
        traits.staging_dir = str(user_build).encode("utf-8")
        return traits

    def __enter__(self) -> "StockRimeSession":
        self.user_data_dir.mkdir(parents=True, exist_ok=True)
        (self.user_data_dir / "logs").mkdir(parents=True, exist_ok=True)
        self.api.dll.RimeSetup(ctypes.byref(self._traits))
        self.api.dll.RimeInitialize(ctypes.byref(self._traits))
        if self.deploy:
            started = bool(self.api.dll.RimeStartMaintenance(True))
            if started:
                self.api.dll.RimeJoinMaintenanceThread()
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.api.dll.RimeFinalize()

    def create_session(self) -> RimeSessionId:
        session_id = self.api.dll.RimeCreateSession()
        if not session_id:
            raise RuntimeError("Failed to create Rime session.")
        return session_id

    def destroy_session(self, session_id: RimeSessionId) -> None:
        self.api.dll.RimeDestroySession(session_id)

    def select_schema(self, session_id: RimeSessionId, schema_id: str) -> None:
        if not self.api.dll.RimeSelectSchema(session_id, schema_id.encode("utf-8")):
            raise RuntimeError(f"Failed to select schema: {schema_id}")

    def set_external_preceding_text(
        self, session_id: RimeSessionId, preceding_text: str
    ) -> None:
        if self.api.set_external_preceding_text_func is None:
            return
        if not self.api.set_external_preceding_text_func(
            session_id, preceding_text.encode("utf-8")
        ):
            raise RuntimeError("Failed to set external preceding text.")

    def clear_external_context(self, session_id: RimeSessionId) -> None:
        if self.api.clear_external_context_func is not None:
            self.api.clear_external_context_func(session_id)

    def feed_input(self, session_id: RimeSessionId, raw_input: str) -> None:
        for char in raw_input:
            processed = self.api.dll.RimeProcessKey(session_id, ord(char), 0)
            if not processed:
                raise RuntimeError(f"Rime rejected key input: {char!r}")

    def get_context(self, session_id: RimeSessionId) -> RimeContext | None:
        ctx = RimeContext()
        init_self_versioned(ctx)
        if not self.api.dll.RimeGetContext(session_id, ctypes.byref(ctx)):
            return None
        return ctx

    def free_context(self, ctx: RimeContext) -> None:
        self.api.dll.RimeFreeContext(ctypes.byref(ctx))

    def collect_candidates(
        self, session_id: RimeSessionId, max_candidates: int, settle_seconds: float
    ) -> list[CandidateSnapshot]:
        deadline = time.monotonic() + settle_seconds
        snapshots: list[CandidateSnapshot] = []
        while time.monotonic() < deadline:
            ctx = self.get_context(session_id)
            if ctx is None:
                time.sleep(0.05)
                continue
            try:
                if ctx.menu.num_candidates > 0 or ctx.composition.length > 0:
                    break
            finally:
                self.free_context(ctx)
            time.sleep(0.05)

        while len(snapshots) < max_candidates:
            ctx = self.get_context(session_id)
            if ctx is None:
                break
            try:
                count = int(ctx.menu.num_candidates)
                for index in range(count):
                    if len(snapshots) >= max_candidates:
                        break
                    candidate = ctx.menu.candidates[index]
                    snapshots.append(
                        CandidateSnapshot(
                            text=decode_cstring(candidate.text),
                            comment=decode_cstring(candidate.comment),
                            rank=len(snapshots) + 1,
                        )
                    )
                is_last_page = bool(ctx.menu.is_last_page)
            finally:
                self.free_context(ctx)

            if is_last_page or len(snapshots) >= max_candidates:
                break
            processed = self.api.dll.RimeProcessKey(session_id, XK_PAGE_DOWN, 0)
            if not processed:
                break
            time.sleep(0.03)
        return snapshots


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a stock Rime + octagram baseline using rime.dll."
    )
    parser.add_argument(
        "--corpus",
        default=r"c:\Code\outwit\witset-worker\tests\test_sentence.txt",
        help="Path to the corpus text file.",
    )
    parser.add_argument(
        "--dll-path",
        default=r"C:\Program Files\Rime\weasel-0.17.4\rime.dll",
        help="Path to the stock rime.dll.",
    )
    parser.add_argument(
        "--shared-data-dir",
        default=r"C:\Program Files\Rime\weasel-0.17.4\data",
        help="Shared data directory used by stock Rime.",
    )
    parser.add_argument(
        "--user-data-dir",
        default=r"C:\Users\Bing\AppData\Roaming\Rime_octagram_bench",
        help="Isolated user data directory for the octagram benchmark.",
    )
    parser.add_argument(
        "--schema",
        default="octagram_bench",
        help="Schema id used for the benchmark.",
    )
    parser.add_argument(
        "--snapshot",
        default=(
            r"C:\Users\Bing\AppData\Roaming\Rime_octagram_bench\debug"
            r"\stock_rime_octagram_snapshot.jsonl"
        ),
        help="Path to the stock Rime snapshot JSONL file.",
    )
    parser.add_argument(
        "--summary-dir",
        default=(
            r"C:\Users\Bing\AppData\Roaming\Rime_octagram_bench\debug"
            r"\snapshot_summary"
        ),
        help="Output directory for reference cases and summary files.",
    )
    parser.add_argument(
        "--max-input-length",
        type=int,
        default=72,
        help="Maximum raw input length for a single text chunk.",
    )
    parser.add_argument(
        "--max-candidates",
        type=int,
        default=20,
        help="Maximum number of candidates collected for each text step.",
    )
    parser.add_argument(
        "--settle-seconds",
        type=float,
        default=0.8,
        help="Maximum initial wait time after feeding raw input.",
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
        "--skip-deploy",
        action="store_true",
        help="Skip maintenance/deployment before running the benchmark.",
    )
    return parser.parse_args()


def snapshot_record(
    *,
    raw_input: str,
    preceding_text: str,
    candidates: list[CandidateSnapshot],
    schema_id: str,
) -> dict[str, Any]:
    return {
        "timestamp_ms": int(time.time() * 1000),
        "session_id": "",
        "schema_id": schema_id,
        "input": raw_input,
        "preceding_text": preceding_text,
        "candidate_count": len(candidates),
        "candidates": [
            {
                "rank": candidate.rank,
                "text": candidate.text,
                "comment": candidate.comment,
                "debug": "",
            }
            for candidate in candidates
        ],
    }


def main() -> int:
    args = parse_args()
    corpus_path = Path(args.corpus).expanduser().resolve()
    dll_path = Path(args.dll_path).expanduser().resolve()
    shared_data_dir = Path(args.shared_data_dir).expanduser().resolve()
    user_data_dir = Path(args.user_data_dir).expanduser().resolve()
    snapshot_path = Path(args.snapshot).expanduser().resolve()
    summary_dir = Path(args.summary_dir).expanduser().resolve()
    reference_path = summary_dir / "reference_cases.jsonl"
    metadata_path = summary_dir / "baseline_run_metadata.json"

    if not corpus_path.is_file():
        raise SystemExit(f"Corpus file not found: {corpus_path}")
    if not dll_path.is_file():
        raise SystemExit(f"Rime DLL not found: {dll_path}")
    if not shared_data_dir.is_dir():
        raise SystemExit(f"Shared data directory not found: {shared_data_dir}")
    if not user_data_dir.is_dir():
        raise SystemExit(f"User data directory not found: {user_data_dir}")

    summary_dir.mkdir(parents=True, exist_ok=True)
    snapshot_path.parent.mkdir(parents=True, exist_ok=True)
    if snapshot_path.exists() and not args.keep_existing_snapshot:
        snapshot_path.unlink()

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

    rime_api = StockRimeApi(dll_path)
    reference_records: list[dict[str, Any]] = []
    snapshot_records: list[dict[str, Any]] = []
    run_metadata: dict[str, Any] = {
        "corpus": str(corpus_path),
        "dll_path": str(dll_path),
        "shared_data_dir": str(shared_data_dir),
        "user_data_dir": str(user_data_dir),
        "snapshot": str(snapshot_path),
        "summary_dir": str(summary_dir),
        "schema": args.schema,
        "max_input_length": args.max_input_length,
        "max_candidates": args.max_candidates,
        "step_count": len(all_steps),
        "text_step_count": len(text_steps),
        "completed_text_steps": 0,
        "expected_not_found_count": 0,
        "external_preceding_supported": bool(
            rime_api.set_external_preceding_text_func is not None
        ),
        "external_preceding_applied": bool(
            rime_api.set_external_preceding_text_func is not None
        ),
        "rime_version": rime_api.version(),
        "deploy_before_run": not args.skip_deploy,
    }

    current_preceding_text = ""
    with StockRimeSession(
        rime_api,
        shared_data_dir=shared_data_dir,
        user_data_dir=user_data_dir,
        app_name="rime.stock_octagram_bench",
        deploy=not args.skip_deploy,
    ) as session:
        for step_index, step in enumerate(all_steps, start=1):
            if step.kind == "text":
                session_id = session.create_session()
                try:
                    session.select_schema(session_id, args.schema)
                    session.set_external_preceding_text(session_id, current_preceding_text)
                    session.feed_input(session_id, step.raw_input)
                    candidates = session.collect_candidates(
                        session_id,
                        max_candidates=args.max_candidates,
                        settle_seconds=args.settle_seconds,
                    )
                    record = snapshot_record(
                        raw_input=step.raw_input,
                        preceding_text=current_preceding_text,
                        candidates=candidates,
                        schema_id=args.schema,
                    )
                    snapshot_records.append(record)
                    selected_rank, expected_rank = choose_rank(record, step.expected_text)
                    if expected_rank is None:
                        run_metadata["expected_not_found_count"] += 1
                    reference_records.append(
                        {
                            "case_id": f"case_{len(reference_records) + 1:06d}",
                            "input": step.raw_input,
                            "preceding_text": current_preceding_text,
                            "snapshot_preceding_text": (
                                current_preceding_text
                                if run_metadata["external_preceding_applied"]
                                else ""
                            ),
                            "expected_text": step.expected_text,
                            "selected_rank": selected_rank,
                            "expected_rank": expected_rank,
                            "source_index": step.source_index,
                            "step_index": step_index,
                        }
                    )
                    run_metadata["completed_text_steps"] += 1
                    if run_metadata["completed_text_steps"] % 50 == 0:
                        print(
                            f"Processed {run_metadata['completed_text_steps']}/"
                            f"{len(text_steps)} text steps..."
                        )
                finally:
                    session.clear_external_context(session_id)
                    session.destroy_session(session_id)
            current_preceding_text += step.expected_text

    write_jsonl(snapshot_path, snapshot_records)
    write_jsonl(reference_path, reference_records)
    write_json(metadata_path, run_metadata)
    run_summary(snapshot_path, summary_dir, reference_path)
    print(f"Stock Rime snapshot written to: {snapshot_path}")
    print(f"Reference cases written to: {reference_path}")
    print(f"Run metadata written to: {metadata_path}")
    print(f"Summary directory: {summary_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
