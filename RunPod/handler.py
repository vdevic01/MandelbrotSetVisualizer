import json
import os
import subprocess
import time

import runpod

WORKER_BIN = os.path.join(os.path.dirname(__file__), "mandelbrot_cuda_worker")


def handler(job: dict) -> dict:
    job_input = job["input"]

    # Pass the entire input object to the worker via stdin as JSON.
    # The worker reads it, deserialises the base64-encoded points array,
    # runs the CUDA kernel, and writes results JSON to stdout.
    stdin_data = json.dumps(job_input).encode()

    t0 = time.perf_counter()
    proc = subprocess.run(
        [WORKER_BIN],
        input=stdin_data,
        capture_output=True,
        timeout=300,
    )
    handler_time_ms = round((time.perf_counter() - t0) * 1000, 3)

    if proc.returncode != 0:
        return {"error": proc.stderr.strip() or "Worker process exited with non-zero code"}

    try:
        output = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return {"error": f"Failed to parse worker output: {exc}\nRaw: {proc.stdout[:400]}"}

    output["handler_time_ms"] = handler_time_ms
    return output


runpod.serverless.start({"handler": handler})
