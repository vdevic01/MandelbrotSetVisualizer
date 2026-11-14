// Reading what the renderer reports. The app starts the renderer with `--format json`, and it then prints one JSON
// object when it is done (see RunReport.h and reportJson in Visualizer/src/main.cpp):
//
//   { "version": 1,
//     "stages": [ { "name": "Coloring", "ms": 12.3 }, ... ],
//     "remote": { "httpStatus": 200, "roundTripMs": 340, "samplingMs": 12.3, "kernelMs": 42.5 },   (remote runs only)
//     "warnings": [ "..." ] }
//
// This file has no dependencies, on purpose: the integration tests (integration-tests/) run it on the output of the
// real renderer, so a change of that output that the app cannot read fails a test.

export const REPORT_VERSION = 1;

export function formatDuration(milliseconds: number): string {
  return milliseconds < 1000 ? `${Math.round(milliseconds)} ms` : `${(milliseconds / 1000).toFixed(2)} s`;
}

// What the renderer reports.
export type RendererReport = {
  version: number,
  stages: { name: string, ms: number }[],
  remote?: { httpStatus: number, roundTripMs?: number, samplingMs?: number, kernelMs?: number },
  warnings: string[]
};

// The report, ready for the status bar.
export type RendererOutput = {
  stages: string,    // "Pixel mapping 64 ms\nCalculating escape iterations 3.2 s\n..." (for a tooltip)
  remote?: string,   // what the remote worker answered, when remote rendering was used
  remoteDetails?: string,
  warnings: string[]
};

// Reads the report. Throws when the text is not a report of the version this app understands (the renderer and the
// app do not belong together), so that the status bar says so instead of silently showing nothing.
export function parseRendererOutput(output: string): RendererOutput {
  const lastLine = output.trim().split(/\r?\n/).pop() ?? "";
  let report: RendererReport;
  try {
    report = JSON.parse(lastLine);
  } catch {
    throw new Error(`The renderer did not print a report (expected one line of JSON, got: ${output.trim().slice(0, 200)})`);
  }
  if (report.version !== REPORT_VERSION) {
    throw new Error(`The renderer reports format version ${report.version}, this app reads version ${REPORT_VERSION}. Rebuild the renderer.`);
  }

  const result: RendererOutput = {
    stages: report.stages.map((s) => `${s.name} ${formatDuration(s.ms)}`).join("\n"),
    warnings: report.warnings ?? []
  };
  if (report.remote) {
    const parts = [`HTTP ${report.remote.httpStatus}`];
    if (report.remote.roundTripMs !== undefined) parts.push(`round trip ${formatDuration(report.remote.roundTripMs)}`);
    if (report.remote.kernelMs !== undefined) parts.push(`kernel ${formatDuration(report.remote.kernelMs)}`);
    if (report.remote.samplingMs !== undefined) parts.push(`sampling ${formatDuration(report.remote.samplingMs)}`);
    result.remote = parts.join(" · ");
    result.remoteDetails = parts.join("\n");
  }
  return result;
}
