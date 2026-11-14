// Integration test of the backend and the renderer: the app (Rust) starts the renderer (C++) and the frontend reads
// what it reports. The real renderer (the C++ binary) is run with the argument lists that the app's own code builds
// (src-tauri/src/cli_args.rs, via dump_args.rs), and the test checks that
//   - every run succeeds and writes a PNG (and the escape file where one is expected), so a flag the renderer
//     renamed, removed or now rejects fails here and not in the app,
//   - every flag the app uses is listed by `--help`,
//   - stdout is exactly one line of JSON of the shape the frontend reads (src/renderer-output.ts), and the
//     frontend's own parser finds in it the stage timings the status bar shows,
//   - for a run in CUDA_REMOTE mode, against a fake worker on localhost: the request the renderer sends has the
//     fields the worker needs, and what the worker answered ends up in the report,
//   - --list-modes lists the modes the mode dropdown is filled from.
//
// Needs the renderer: MANDELBROT_VISUALIZER_BIN=<path to mandelbrot_visualizer>, or the copy the CMake build
// deploys to src-tauri/binaries. Needs `rustc` (it ships with the Rust toolchain the app is built with).
//
// Run: node integration-tests/check-cli.mjs

import { execFile, execFileSync } from "node:child_process";
import fs from "node:fs";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import { promisify } from "node:util";
import { fileURLToPath, pathToFileURL } from "node:url";
import ts from "typescript";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const errors = [];
const fail = (message) => errors.push(message);

function findRenderer() {
  if (process.env.MANDELBROT_VISUALIZER_BIN) return process.env.MANDELBROT_VISUALIZER_BIN;
  const deployed = path.join(root, "src-tauri/binaries");
  const found = fs.existsSync(deployed) ? fs.readdirSync(deployed).find((f) => f.startsWith("mandelbrot_visualizer-")) : undefined;
  if (found) return path.join(deployed, found);
  console.error("Cannot find the renderer. Build it (cmake --build) or set MANDELBROT_VISUALIZER_BIN.");
  process.exit(2);
}
const renderer = findRenderer();

const work = fs.mkdtempSync(path.join(os.tmpdir(), "mandelbrot-integration-"));
const run = (file, args, options = {}) => execFileSync(file, args, { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], ...options });
const runAsync = promisify(execFile);

// A stand-in for the CUDA worker: checks the request the renderer sends and answers like the real worker (the format
// is described at the top of Worker/src/main_worker.cpp): base64 iterations (int32) and escape values (float32).
const FAKE_WORKER = { samplingMs: 1.5, kernelMs: 2.5, apiKey: "integration-test-key" };
const requests = [];
const worker = http.createServer((req, res) => {
  let body = "";
  req.on("data", (chunk) => (body += chunk));
  req.on("end", () => {
    let request;
    try {
      request = JSON.parse(body);
    } catch {
      fail(`the renderer sent a request to the worker that is not JSON: ${body.slice(0, 100)}`);
      res.writeHead(400).end();
      return;
    }
    requests.push(request);
    if (req.headers.authorization !== `Bearer ${FAKE_WORKER.apiKey}`) fail("the renderer did not send the API key as a Bearer token");
    for (const field of ["use_hp", "max_iter", "samples", "width", "height", "re_start", "re_end", "im_start", "im_end", "escape_radius"])
      if (!(field in request)) fail(`the request to the worker has no '${field}' (it has: ${Object.keys(request).join(", ")})`);
    const count = request.width * request.height * request.samples;
    const iterations = Buffer.alloc(count * 4);
    for (let i = 0; i < count; i++) iterations.writeInt32LE(-1, i * 4);   // no sample escaped: a black image
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({
      sampling_time_ms: FAKE_WORKER.samplingMs,
      execution_time_ms: FAKE_WORKER.kernelMs,
      iterations: iterations.toString("base64"),
      escape_values: Buffer.alloc(count * 4).toString("base64"),
    }));
  });
});
await new Promise((resolve) => worker.listen(0, "127.0.0.1", resolve));
const workerUrl = `http://127.0.0.1:${worker.address().port}/`;

try {
  // The argument lists, built by the app's code.
  const dumper = path.join(work, process.platform === "win32" ? "dump_args.exe" : "dump_args");
  run("rustc", ["--edition", "2021", "-o", dumper, path.join(root, "integration-tests/dump_args.rs")]);
  const scenarios = JSON.parse(run(dumper, [work]));

  // The frontend's parser of the renderer's report, compiled from the TypeScript source.
  const source = fs.readFileSync(path.join(root, "src/renderer-output.ts"), "utf8");
  const compiled = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2020 } });
  const parserFile = path.join(work, "renderer-output.mjs");
  fs.writeFileSync(parserFile, compiled.outputText);
  const { parseRendererOutput, REPORT_VERSION } = await import(pathToFileURL(parserFile).href);

  // Every flag the app uses is known to the renderer.
  const help = run(renderer, ["--help"]);
  const known = new Set([...help.matchAll(/^\s+(--[a-z][a-z-]*)/gm)].map((m) => m[1]));
  const used = new Set(scenarios.flatMap((s) => s.args).filter((a) => a.startsWith("--")).map((a) => a.split("=")[0]));
  const cliArgsSource = fs.readFileSync(path.join(root, "src-tauri/src/cli_args.rs"), "utf8");
  for (const [, flag] of cliArgsSource.matchAll(/"(--[a-z][a-z-]*)/g)) used.add(flag);
  for (const flag of used) if (!known.has(flag)) fail(`the app uses ${flag}, which the renderer does not list in --help`);

  // Real runs (in order: a recolour reads the escape file of the render before it).
  for (const s of scenarios) {
    let stdout;
    try {
      const env = s.remote ? { ...process.env, REMOTE_ENDPOINT: workerUrl, REMOTE_API_KEY: FAKE_WORKER.apiKey } : process.env;
      ({ stdout } = await runAsync(renderer, s.args, { cwd: work, env, encoding: "utf8" }));
    } catch (e) {
      fail(`${s.name}: the renderer failed with the app's arguments\n      ${s.args.join(" ")}\n      ${String(e.stderr || e.message).trim().split("\n").join("\n      ")}`);
      continue;
    }
    if (!fs.existsSync(s.output)) {
      fail(`${s.name}: the renderer did not write ${s.output}`);
    } else if (fs.readFileSync(s.output).subarray(0, 8).toString("hex") !== "89504e470d0a1a0a") {
      fail(`${s.name}: ${s.output} is not a PNG`);
    }
    if (s.escapeFile && !fs.existsSync(s.escapeFile)) fail(`${s.name}: the renderer did not write the escape file ${s.escapeFile}`);

    // stdout is the report and nothing else.
    const lines = stdout.split(/\r?\n/).filter((l) => l.trim());
    let report;
    try {
      report = JSON.parse(lines[0]);
    } catch {
      fail(`${s.name}: the first line of the renderer's output is not JSON: ${JSON.stringify(stdout.slice(0, 200))}`);
      continue;
    }
    if (lines.length !== 1) fail(`${s.name}: the renderer printed ${lines.length} lines with --format json, expected the report only`);
    if (report.version !== REPORT_VERSION) fail(`${s.name}: the report has version ${report.version}, the app reads ${REPORT_VERSION}`);
    if (!Array.isArray(report.stages) || !report.stages.every((st) => typeof st.name === "string" && typeof st.ms === "number"))
      fail(`${s.name}: 'stages' in the report is not a list of { name, ms }: ${JSON.stringify(report.stages)}`);
    if (!Array.isArray(report.warnings)) fail(`${s.name}: 'warnings' in the report is not a list`);

    // What the frontend makes of it.
    const parsed = parseRendererOutput(stdout);
    for (const stage of s.stages)
      if (!new RegExp(`^${stage.replace(/[()+]/g, "\\$&")} \\d+(\\.\\d+)? m?s$`, "m").test(parsed.stages))
        fail(`${s.name}: the app reads no '${stage}' time from the report (it read: ${JSON.stringify(parsed.stages)})`);

    if (s.remote) {
      const remote = report.remote;
      if (!remote) fail(`${s.name}: no 'remote' in the report of a run with the remote worker`);
      else {
        if (remote.httpStatus !== 200) fail(`${s.name}: remote.httpStatus is ${remote.httpStatus}, the worker answered 200`);
        if (typeof remote.roundTripMs !== "number" || remote.roundTripMs < 0) fail(`${s.name}: remote.roundTripMs is not a duration: ${remote.roundTripMs}`);
        if (remote.samplingMs !== FAKE_WORKER.samplingMs) fail(`${s.name}: remote.samplingMs is ${remote.samplingMs}, the worker sent ${FAKE_WORKER.samplingMs}`);
        if (remote.kernelMs !== FAKE_WORKER.kernelMs) fail(`${s.name}: remote.kernelMs is ${remote.kernelMs}, the worker sent ${FAKE_WORKER.kernelMs}`);
      }
      if (!parsed.remote?.startsWith("HTTP 200")) fail(`${s.name}: the app shows '${parsed.remote}' for the remote worker, expected it to start with 'HTTP 200'`);
      if (requests.length === 0) fail(`${s.name}: the renderer sent no request to the worker`);
    } else if (report.remote) {
      fail(`${s.name}: the report has 'remote' although no remote worker was used`);
    }
  }

  // The modes the app offers are the ones the renderer lists.
  const modes = run(renderer, ["--list-modes"]).split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
  if (!modes.includes("CPU_PARALLEL")) fail(`--list-modes does not list CPU_PARALLEL (it listed: ${modes.join(", ") || "nothing"})`);
  const html = fs.readFileSync(path.join(root, "index.html"), "utf8");
  for (const mode of modes)
    if (!html.includes(`value="${mode}"`)) fail(`the renderer lists the mode ${mode}, but the mode dropdown in index.html has no option for it`);

  if (!errors.length)
    console.log(`Integration test ok: ${scenarios.length} runs of the real renderer with the app's arguments, ${used.size} flags known, modes: ${modes.join(", ")}`);
} catch (e) {
  fail(`${e.message}${e.stderr ? `\n${e.stderr}` : ""}`);
} finally {
  worker.close();
  fs.rmSync(work, { recursive: true, force: true });
}

if (errors.length) {
  console.error(`Integration test failed (${errors.length}):\n  - ${errors.join("\n  - ")}`);
  process.exit(1);
}
