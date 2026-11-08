import http.server
import itertools
import json
import os
import platform
import shlex
import socketserver
import subprocess
import sys
import threading
import time
import traceback

DEFAULT_WORKER_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mandelbrot_cuda_worker")
# WORKER_COMMAND can override the worker program (used for testing the handler without a GPU).
WORKER_CMD = shlex.split(os.environ.get("WORKER_COMMAND", "")) or [DEFAULT_WORKER_BIN]
PORT = int(os.environ.get("PORT", 80))
LOG_PING = os.environ.get("LOG_PING", "0") == "1"   # health checks are frequent; only log them when asked
WORKER_TIMEOUT_S = 300

STARTED_AT = time.time()
_request_ids = itertools.count(1)
_stats_lock = threading.Lock()
_stats = {"requests": 0, "ok": 0, "errors": 0}
_ping_logged = False


def log(message: str) -> None:
    print(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {message}", flush=True)


def count(key: str) -> None:
    with _stats_lock:
        _stats[key] += 1


def gpu_info() -> str:
    """Best-effort description of the GPU(s); never raises."""
    try:
        out = subprocess.run(
            ["nvidia-smi", "--query-gpu=name,driver_version,memory.total", "--format=csv,noheader"],
            capture_output=True, text=True, timeout=10,
        )
        text = out.stdout.strip() or out.stderr.strip()
        return text if out.returncode == 0 and text else f"nvidia-smi failed (exit {out.returncode}): {text}"
    except Exception as exc:  # missing binary, timeout, ...
        return f"unavailable ({exc})"


def describe_request(job_input: dict) -> str:
    keys = ("use_hp", "width", "height", "samples", "max_iter")
    return " ".join(f"{k}={job_input.get(k)}" for k in keys)


def run_worker(job_input: dict, rid: int) -> dict:
    stdin_data = json.dumps(job_input, separators=(',', ':'))
    t0 = time.perf_counter()
    try:
        proc = subprocess.run(
            WORKER_CMD,
            input=stdin_data,
            capture_output=True,
            text=True,
            timeout=WORKER_TIMEOUT_S,
        )
    except subprocess.TimeoutExpired:
        log(f"[{rid}] worker timed out after {WORKER_TIMEOUT_S}s")
        return {"error": f"Worker timed out after {WORKER_TIMEOUT_S} s"}
    except OSError as exc:
        log(f"[{rid}] could not start the worker program {WORKER_CMD[0]}: {exc}")
        return {"error": f"Could not start worker program: {exc}"}
    handler_time_ms = round((time.perf_counter() - t0) * 1000, 3)

    log(f"[{rid}] worker exited with code {proc.returncode} in {handler_time_ms} ms "
        f"(stdout {len(proc.stdout)} bytes, stderr {len(proc.stderr)} bytes)")
    if proc.stderr.strip():
        log(f"[{rid}] worker stderr: {proc.stderr.strip()[:2000]}")

    if proc.returncode != 0:
        return {"error": proc.stderr.strip() or f"Worker exited with code {proc.returncode}"}

    try:
        output = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return {"error": f"Failed to parse worker output: {exc}\nRaw: {proc.stdout[:400]}"}

    output["handler_time_ms"] = handler_time_ms
    return output


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        global _ping_logged
        if self.path == "/ping":
            if LOG_PING or not _ping_logged:
                log(f"GET /ping from {self.address_string()}"
                    + ("" if LOG_PING else " (further pings are not logged; set LOG_PING=1 to log them)"))
                _ping_logged = True
            self._send_text(200, "OK")
        elif self.path == "/info":
            with _stats_lock:
                stats = dict(_stats)
            self._respond(200, {
                "uptime_s": round(time.time() - STARTED_AT),
                "worker_command": WORKER_CMD,
                "gpu": gpu_info(),
                **stats,
            })
        else:
            log(f"GET {self.path} from {self.address_string()} -> 404")
            self._send_text(404, "Not found")

    def do_POST(self):
        rid = next(_request_ids)
        t0 = time.perf_counter()
        count("requests")
        length = int(self.headers.get("Content-Length", 0))
        log(f"[{rid}] POST {self.path} from {self.address_string()} ({length} bytes)")
        try:
            body = self.rfile.read(length)
            try:
                job_input = json.loads(body)
            except json.JSONDecodeError as exc:
                log(f"[{rid}] invalid JSON body: {exc}; first 200 bytes: {body[:200]!r}")
                self._finish(rid, t0, 400, {"error": f"Invalid JSON body: {exc}"})
                return
            log(f"[{rid}] request: {describe_request(job_input)}")
            result = run_worker(job_input, rid)
            self._finish(rid, t0, 500 if "error" in result else 200, result)
        except Exception as exc:
            log(f"[{rid}] unexpected error: {exc}\n{traceback.format_exc()}")
            self._finish(rid, t0, 500, {"error": f"{type(exc).__name__}: {exc}"})

    def _finish(self, rid: int, t0: float, code: int, data: dict) -> None:
        count("ok" if code == 200 else "errors")
        payload = self._respond(code, data)
        total_ms = round((time.perf_counter() - t0) * 1000, 1)
        suffix = "" if code == 200 else f" error: {str(data.get('error'))[:300]}"
        log(f"[{rid}] -> {code} ({payload} bytes) in {total_ms} ms{suffix}")

    def _send_text(self, code: int, text: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(text.encode())

    def _respond(self, code: int, data: dict) -> int:
        payload = json.dumps(data, separators=(',', ':')).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
        return len(payload)

    def log_message(self, fmt, *args):
        pass  # replaced by the explicit request logging above


class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def log_startup() -> None:
    log(f"mandelbrot worker handler starting: python {platform.python_version()} on {platform.platform()}")
    log(f"port {PORT}, worker command: {WORKER_CMD}")
    exe = WORKER_CMD[0]
    if os.path.isabs(exe):
        if not os.path.isfile(exe):
            log(f"WARNING: worker program {exe} does not exist")
        elif not os.access(exe, os.X_OK):
            log(f"WARNING: worker program {exe} is not executable")
    log(f"GPU: {gpu_info()}")


if __name__ == "__main__":
    log_startup()
    server = ThreadedHTTPServer(("0.0.0.0", PORT), Handler)
    log(f"Listening on port {PORT}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("shutting down")
        sys.exit(0)
