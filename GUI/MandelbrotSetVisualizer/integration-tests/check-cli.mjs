// Integration test of the backend and the renderer: the app (Rust) starts the renderer (C++) and the frontend reads what it prints. Runs the real renderer (the C++ binary) with the argument
// lists that the app's own code builds (src-tauri/src/cli_args.rs, via dump_args.rs) and checks that
//   - every run succeeds and writes a PNG (and the escape file where one is expected), so a flag the renderer
//     renamed, removed or now rejects fails here and not in the app,
//   - every flag the app uses is listed by `--help`,
//   - the stage timings the frontend reads from the output are found by the frontend's own parser
//     (src/renderer-output.ts), and --list-modes lists the modes the mode dropdown is filled from.
//
// Needs the renderer: MANDELBROT_VISUALIZER_BIN=<path to mandelbrot_visualizer>, or the copy the CMake build
// deploys to src-tauri/binaries. Needs `rustc` (it ships with the Rust toolchain the app is built with).
//
// Run: node integration-tests/check-cli.mjs

import { execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
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

try {
  // The argument lists, built by the app's code.
  const dumper = path.join(work, process.platform === "win32" ? "dump_args.exe" : "dump_args");
  run("rustc", ["--edition", "2021", "-o", dumper, path.join(root, "integration-tests/dump_args.rs")]);
  const scenarios = JSON.parse(run(dumper, [work]));

  // The frontend's parser of the renderer's output, compiled from the TypeScript source.
  const source = fs.readFileSync(path.join(root, "src/renderer-output.ts"), "utf8");
  const compiled = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2020 } });
  const parserFile = path.join(work, "renderer-output.mjs");
  fs.writeFileSync(parserFile, compiled.outputText);
  const { parseRendererOutput } = await import(pathToFileURL(parserFile).href);

  // Every flag the app uses is known to the renderer.
  const help = run(renderer, ["--help"]);
  const known = new Set([...help.matchAll(/^\s+(--[a-z][a-z-]*)/gm)].map((m) => m[1]));
  const used = new Set(scenarios.flatMap((s) => s.args).filter((a) => a.startsWith("--")).map((a) => a.split("=")[0]));
  const cliArgsSource = fs.readFileSync(path.join(root, "src-tauri/src/cli_args.rs"), "utf8");
  for (const [, flag] of cliArgsSource.matchAll(/"(--[a-z][a-z-]*)/g)) used.add(flag);
  for (const flag of used) if (!known.has(flag)) fail(`the app uses ${flag}, which the renderer does not list in --help`);

  // Real runs.
  for (const s of scenarios) {
    let stdout;
    try {
      stdout = run(renderer, s.args, { cwd: work });
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

    const stages = parseRendererOutput(stdout).stages;
    for (const stage of s.stages)
      if (!new RegExp(`^${stage} \\d+(\\.\\d+)? m?s$`, "m").test(stages))
        fail(`${s.name}: the app reads no '${stage}' time from the renderer's output (it read: ${JSON.stringify(stages)})`);
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
  fs.rmSync(work, { recursive: true, force: true });
}

if (errors.length) {
  console.error(`Integration test failed (${errors.length}):\n  - ${errors.join("\n  - ")}`);
  process.exit(1);
}
