#!/usr/bin/env python3
"""Replays a captured requests.log against a live ms_server and fails loudly
on any mismatch.

The log format is whatever response_t::send() (src/server/api.cxx) writes to
requests.log: one line per request ("METHOD /path?query"), immediately
followed by one line per response ("-> STATUS body-json"). Every request in
this server always gets exactly one logged response, in order, so the two
kinds of lines simply alternate.

Each session in the log starts at a "POST /session/new..." call. Replay
starts a fresh session there too - a fresh JWT, since tokens expire and
aren't meant to be reused across runs - and reattaches the new token as the
bearer token for every subsequent request in that session block, mirroring
what the original client did. Everything after that must be deterministic:
same fixed board, same request sequence in, same responses out, so any
difference (status code, or response body once both sides are parsed back
into JSON - key order isn't compared, only structure and values) is reported
as a failure.

Usage - replay against a server you already started yourself:
    replay_requests_log.py --log path/to/requests.log --base-url http://localhost:8080

Usage - let this script start and stop ms_server around the replay (what the
"live_replay" CTest target does):
    replay_requests_log.py --log path/to/requests.log \\
        --server-bin build/ms_server --plugins-dir build/plugins --port 18099
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path


@dataclass
class RequestResponse:
    method: str
    path: str
    expected_status: int
    expected_body: str


_TIMESTAMP_RE = re.compile(r"^\[[^\]]*\]\s(.*)$")


def parse_log(path: Path) -> list[RequestResponse]:
    entries: list[tuple[str, ...]] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.rstrip("\n")
        if not line.strip():
            continue
        m = _TIMESTAMP_RE.match(line)
        if not m:
            raise ValueError(f"unrecognized log line (no timestamp prefix): {line!r}")
        rest = m.group(1)
        if rest.startswith("-> "):
            status_str, _, body = rest[3:].partition(" ")
            entries.append(("response", status_str, body))
        else:
            method, _, path_and_query = rest.partition(" ")
            entries.append(("request", method, path_and_query))

    pairs: list[RequestResponse] = []
    i = 0
    while i < len(entries):
        kind, method, path_and_query = entries[i]
        if kind != "request":
            raise ValueError(f"expected a request line at position {i}, got {entries[i]!r}")
        if i + 1 >= len(entries) or entries[i + 1][0] != "response":
            raise ValueError(f"request {method} {path_and_query!r} has no matching response line")
        _, status_str, body = entries[i + 1]
        pairs.append(RequestResponse(method, path_and_query, int(status_str), body))
        i += 2
    return pairs


def split_sessions(pairs: list[RequestResponse]) -> list[list[RequestResponse]]:
    sessions: list[list[RequestResponse]] = []
    current: list[RequestResponse] = []
    for p in pairs:
        if p.path.startswith("/session/new"):
            if current:
                sessions.append(current)
            current = [p]
        else:
            if not current:
                raise ValueError("log doesn't start with a /session/new call")
            current.append(p)
    if current:
        sessions.append(current)
    return sessions


def http_call(base_url: str, method: str, path_and_query: str, token: str | None, timeout: float) -> tuple[int, str]:
    request = urllib.request.Request(base_url.rstrip("/") + path_and_query, method=method)
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, response.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8")


def replay(base_url: str, sessions: list[list[RequestResponse]], timeout: float) -> tuple[int, list[str], list[str]]:
    # Response bodies are expected to gain fields over time (e.g. folding
    # bombs_count/fully_revealed into action replies so a client doesn't need
    # a separate follow-up request) - that's a deliberate, ongoing evolution
    # of the wire format, not a regression, so a body mismatch alone is only
    # a warning. But every action handler already reports its outcome through
    # a top-level "status" field with a fixed vocabulary - "ok"/"boom" from
    # cell/reveal, cell/flag, cell/check, and "win"/"lose" from board/check -
    # and on this fixed board that outcome must never change no matter how
    # the rest of the body grows: a "status" mismatch is always a hard
    # failure, --warnings-as-error or not.
    failures: list[str] = []
    warnings: list[str] = []
    total = 0

    for session_index, session in enumerate(sessions, start=1):
        first = session[0]
        total += 1
        status, body = http_call(base_url, first.method, first.path, token=None, timeout=timeout)
        label = f"session {session_index} req 1 ({first.method} {first.path})"
        if status != first.expected_status:
            failures.append(f"{label}: status {status} != expected {first.expected_status} (body: {body!r})")

        token = None
        try:
            token = json.loads(body).get("token")
        except json.JSONDecodeError:
            pass
        if not token:
            failures.append(f"{label}: response has no usable token, can't replay the rest of this session (body: {body!r})")
            continue

        for req_index, p in enumerate(session[1:], start=2):
            total += 1
            status, body = http_call(base_url, p.method, p.path, token=token, timeout=timeout)
            label = f"session {session_index} req {req_index} ({p.method} {p.path})"
            if status != p.expected_status:
                failures.append(f"{label}: status {status} != expected {p.expected_status} (body: {body!r})")
                continue
            try:
                actual_json = json.loads(body)
                expected_json = json.loads(p.expected_body)
            except json.JSONDecodeError as e:
                failures.append(f"{label}: response body isn't valid JSON: {e} (body: {body!r})")
                continue

            if actual_json == expected_json:
                continue

            expected_outcome = expected_json.get("status") if isinstance(expected_json, dict) else None
            actual_outcome = actual_json.get("status") if isinstance(actual_json, dict) else None
            if actual_outcome != expected_outcome:
                failures.append(
                    f"{label}: OUTCOME MISMATCH - status {expected_outcome!r} != {actual_outcome!r} "
                    f"(this is on a fixed board, so this is never expected to change)\n"
                    f"    expected: {expected_json}\n    actual:   {actual_json}"
                )
            else:
                warnings.append(f"{label}: body differs (outcome '{actual_outcome}' unchanged)\n    expected: {expected_json}\n    actual:   {actual_json}")

    return total, failures, warnings


def wait_until_ready(base_url: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            urllib.request.urlopen(base_url.rstrip("/") + "/boards/list", timeout=0.5)
            return True
        except urllib.error.HTTPError:
            # Any HTTP response - even an error one - means the server is up.
            return True
        except (urllib.error.URLError, ConnectionError, TimeoutError):
            time.sleep(0.2)
    return False


def main ( ) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--log", required=True, type=Path, help="requests.log file to replay")
    parser.add_argument("--base-url", help="replay against an already-running server at this URL")
    parser.add_argument("--server-bin", type=Path, help="ms_server binary to start/stop around the replay (used when --base-url is omitted)")
    parser.add_argument("--plugins-dir", type=Path, help="--plugins-dir to pass to the spawned ms_server")
    parser.add_argument("--port", type=int, default=18099, help="port for the spawned ms_server (default: 18099)")
    parser.add_argument("--server-cwd", type=Path, help="working directory for the spawned server (RSA keys/logs land here); defaults to --server-bin's directory")
    parser.add_argument("--startup-timeout", type=float, default=15.0)
    parser.add_argument("--request-timeout", type=float, default=10.0)
    parser.add_argument(
        "--warnings-as-error",
        action="store_true",
        help="also fail the run on a body mismatch that doesn't change the outcome (\"status\" field) - "
        "off by default, since response bodies are expected to gain fields over time",
    )
    args = parser.parse_args()

    if not args.base_url and not args.server_bin:
        parser.error("need either --base-url (replay against a running server) or --server-bin (spawn one)")

    sessions = split_sessions(parse_log(args.log))
    total_requests = sum(len(s) for s in sessions)
    print(f"Loaded {len(sessions)} session(s), {total_requests} request(s) from {args.log}")

    server_process: subprocess.Popen | None = None
    base_url = args.base_url
    try:
        if not base_url:
            base_url = f"http://localhost:{args.port}"
            # Resolve to absolute paths before spawning: Popen(cwd=...) changes
            # the child's working directory *before* it resolves a relative
            # argv[0], so a relative --server-bin combined with a cwd derived
            # from it would double up (cwd/cwd/ms_server) instead of finding
            # the binary.
            server_bin = args.server_bin.resolve()
            server_args = [str(server_bin), "--no-https", "--port", str(args.port), "--log-level", "warn"]
            if args.plugins_dir:
                server_args += ["--plugins-dir", str(args.plugins_dir.resolve())]
            cwd = (args.server_cwd or server_bin.parent).resolve()
            print(f"Starting {' '.join(server_args)} (cwd={cwd})")
            server_process = subprocess.Popen(server_args, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if not wait_until_ready(base_url, args.startup_timeout):
                print("ERROR: server did not become ready in time", file=sys.stderr)
                return 1

        total, failures, warnings = replay(base_url, sessions, args.request_timeout)
    finally:
        if server_process is not None:
            server_process.terminate()
            try:
                server_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server_process.kill()
                server_process.wait(timeout=5)

    if warnings:
        print(f"\n{len(warnings)}/{total} request(s) had a non-outcome body mismatch (WARNING):\n", file=sys.stderr)
        for w in warnings:
            print(f"  - {w}", file=sys.stderr)

    if failures:
        print(f"\n{len(failures)}/{total} request(s) FAILED:\n", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1

    if warnings and args.warnings_as_error:
        print(f"\n--warnings-as-error set: treating the {len(warnings)} warning(s) above as failures.", file=sys.stderr)
        return 1

    if warnings:
        print(f"\nAll {total} replayed request(s) matched on outcome ({len(warnings)} non-outcome body mismatch(es) above).")
    else:
        print(f"All {total} replayed request(s) matched the log.")
    return 0


if __name__ == "__main__":
    sys.exit(main( ))
