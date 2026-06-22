import http.server
import json
import os
import socketserver
import subprocess
import threading
import time

WORKER_BIN = os.path.join(os.path.dirname(__file__), "mandelbrot_cuda_worker")
PORT        = int(os.environ.get("PORT",        8080))
PORT_HEALTH = int(os.environ.get("PORT_HEALTH", 8081))


def run_worker(job_input: dict) -> dict:
    stdin_data = json.dumps(job_input, separators=(',', ':'))
    t0 = time.perf_counter()
    proc = subprocess.run(
        [WORKER_BIN],
        input=stdin_data,
        capture_output=True,
        text=True,
        timeout=300,
    )
    handler_time_ms = round((time.perf_counter() - t0) * 1000, 3)

    if proc.returncode != 0:
        return {"error": proc.stderr.strip() or "Worker exited with non-zero code"}

    try:
        output = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return {"error": f"Failed to parse worker output: {exc}\nRaw: {proc.stdout[:400]}"}

    output["handler_time_ms"] = handler_time_ms
    return output


class MainHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        try:
            result = run_worker(json.loads(body))
            self._respond(200, result)
        except Exception as exc:
            self._respond(500, {"error": str(exc)})

    def _respond(self, code: int, data: dict):
        payload = json.dumps(data, separators=(',', ':')).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        print(f"[main] {self.address_string()} {fmt % args}", flush=True)


class HealthHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/ping":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"OK")
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, fmt, *args):
        pass  # suppress health-check noise


class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


if __name__ == "__main__":
    health_server = ThreadedHTTPServer(("0.0.0.0", PORT_HEALTH), HealthHandler)
    threading.Thread(target=health_server.serve_forever, daemon=True).start()
    print(f"Health server listening on port {PORT_HEALTH}", flush=True)

    main_server = ThreadedHTTPServer(("0.0.0.0", PORT), MainHandler)
    print(f"Main server listening on port {PORT}", flush=True)
    main_server.serve_forever()
