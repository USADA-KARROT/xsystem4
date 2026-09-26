#!/usr/bin/env python3
"""Read existing run logs only; never attach, launch, sample, or stop a process."""
import argparse
import collections
import json
import re
from pathlib import Path


def fields(text):
    result = {}
    for key, value in re.findall(r"(\w+)=([^\s]+)", text):
        try:
            result[key] = int(value, 16) if value.lower().startswith("0x") else float(value) if "." in value else int(value)
        except ValueError:
            result[key] = value
    return result


def summarize(run, samples=None):
    log = (run / "engine.log").read_text(errors="replace").splitlines()
    meta_path = run / "run.json"
    meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
    windows, messages, errors = [], [], collections.Counter()
    calls, active = {}, {}
    for lineno, line in enumerate(log, 1):
        if "STAGE2_PERF " in line:
            row = fields(line.split("STAGE2_PERF ", 1)[1])
            row["line"] = lineno
            windows.append(row)
        message = re.match(r"^MSG (\d+):", line)
        if message:
            messages.append({"line": lineno, "message_index": int(message[1])})
        for name, pattern in {
            "system_error_log_lines": r"system\.Error:",
            "vm_call_timeout_log_lines": r"VM_CALL_TIMEOUT:",
            "double_free_log_lines": r"double.free",
            "sanitizer_diagnostic_log_lines": r"ERROR: AddressSanitizer|runtime error:|SUMMARY:.*Sanitizer",
            "segfault_signal_log_lines": r"SIGSEGV caught",
        }.items():
            if re.search(pattern, line, re.I):
                errors[name] += 1
        event = re.search(r"STAGE2 (enter|return)\s+(.*)", line)
        if not event:
            continue
        row = fields(event[2])
        if not all(k in row for k in ("f", "hit", "depth")):
            continue
        key = (row["f"], row["hit"], row["depth"])
        info = calls.setdefault(str(row["f"]), {"enters": 0, "returns": 0, "return_without_entry": 0, "elapsed_ms": []})
        if event[1] == "enter":
            info["enters"] += 1
            active[key] = {"line": lineno, **row}
        else:
            info["returns"] += 1
            if active.pop(key, None) is None:
                info["return_without_entry"] += 1
            if isinstance(row.get("elapsed"), (int, float)):
                info["elapsed_ms"].append(row["elapsed"])
    duration = sum(r["window_ms"] for r in windows)
    presents = sum(r["presents"] for r in windows)
    perf = {
        "reported_windows": len(windows), "reported_duration_ms": duration,
        "reported_presents": presents,
        "weighted_present_fps": presents * 1000 / duration if duration else None,
        "max_interval_ms": max((r["max_interval_ms"] for r in windows), default=None),
        "max_swap_ms": max((r["max_swap_ms"] for r in windows), default=None),
        "weighted_mean_swap_ms": sum(r["avg_swap_ms"] * r["presents"] for r in windows) / presents if presents else None,
        "per_window_p95_interval_ms_range": [min(r["p95_interval_ms"] for r in windows), max(r["p95_interval_ms"] for r in windows)] if windows else None,
        "sample_capped_windows": sum(r["samples"] < r["presents"] for r in windows),
        "last_reported_sdl_ticks_ms": windows[-1]["t_ms"] if windows else None,
        "windows_with_at_least_one_interval_over_ms": {str(t): sum(r["max_interval_ms"] > t for r in windows) for t in (33.333, 50, 100, 1000)},
        "end_of_window_state_counts": {
            "shown": sum(bool(r["window_flags"] & 4) for r in windows),
            "minimized": sum(bool(r["window_flags"] & 64) for r in windows),
            "input_focus": sum(bool(r["window_flags"] & 512) for r in windows),
            "vsync_values": sorted({r["vsync"] for r in windows}),
        },
    }
    for info in calls.values():
        values = info["elapsed_ms"]
        info["max_elapsed_ms"] = max(values, default=None)
        info["mean_elapsed_ms"] = sum(values) / len(values) if values else None
    rss = None
    if samples:
        rows = [json.loads(line) for line in samples.read_text().splitlines() if line.strip()]
        rows = sorted((r for r in rows if isinstance(r.get("elapsed_s"), (int, float)) and isinstance(r.get("rss_kib"), (int, float))), key=lambda r: r["elapsed_s"])
        if rows:
            rss = {"samples": len(rows), "first_elapsed_s": rows[0]["elapsed_s"], "last_elapsed_s": rows[-1]["elapsed_s"],
                   "first_rss_mib": rows[0]["rss_kib"] / 1024, "last_rss_mib": rows[-1]["rss_kib"] / 1024,
                   "peak_observed_rss_mib": max(r["rss_kib"] for r in rows) / 1024,
                   "observed_delta_rss_mib": (rows[-1]["rss_kib"] - rows[0]["rss_kib"]) / 1024,
                   "peak_observed_cpu_pct": max((r["cpu_pct"] for r in rows if isinstance(r.get("cpu_pct"), (int, float))), default=None)}
    return {"run": str(run), "exit_code": meta.get("exit_code"), "elapsed_seconds": meta.get("elapsed_seconds"),
            "stop_reason": meta.get("stop_reason"),
            "stopped_by_runner": meta.get("stopped_by_runner", bool(meta.get("stop_reason"))),
            "child_exit_peak_rss_bytes": meta.get("peak_rss_bytes"), "perf": perf, "rss": rss,
            "message_count": len(messages), "messages": messages, "trace_calls": calls,
            "traced_entries_without_return_at_log_end": list(active.values()), "diagnostic_log_line_counts": dict(errors),
            "windows": windows,
            "limits": ["Present rate can include identical visual content; progress is separate.",
                       "No whole-run p95 or exact long-frame count can be reconstructed from window summaries.",
                       "Initial interval and unfinished final window/stall are unreported; a missing log report is not zero FPS.",
                       "Window flags describe report-time state, not every frame in the window.",
                       "Trace elapsed is inclusive wall time with nested work, waits, and logging; bounded first hits are not an unbiased profile.",
                       "An unmatched trace entry may be in flight, terminated, or otherwise unwound; it is not proof of a permanent hang.",
                       "RSS is process resident memory, not live AIN heap or proof of a leak; its sampled peak can miss brief peaks.",
                       "Diagnostic counts are emitted lines, not exact API error counts; source suppresses repeated warnings."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--samples", type=Path, help="Existing JSONL rows: elapsed_s, rss_kib, optional cpu_pct")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    text = json.dumps(summarize(args.run, args.samples), ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
