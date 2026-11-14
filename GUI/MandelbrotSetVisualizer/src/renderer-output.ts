// Reading what the renderer prints. This file has no dependencies, on purpose: the contract tests
// (contract-tests/) run it on the output of the real renderer, so a change of that output that the app cannot read
// fails a test.

export function formatDuration(milliseconds: number): string {
  return milliseconds < 1000 ? `${Math.round(milliseconds)} ms` : `${(milliseconds / 1000).toFixed(2)} s`;
}

export type RendererOutput = {
  stages: string,    // "Pixel mapping 64 ms, Calculating escape iterations 3.2 s, ..." (for a tooltip)
  remote?: string,   // what the remote worker answered, when remote rendering was used
  remoteDetails?: string
};

// Reads what the renderer printed: the time of each stage and, for remote rendering, the worker's answer.
export function parseRendererOutput(output: string): RendererOutput {
  const stages: string[] = [];
  let httpStatus: string | undefined;
  let roundTripMs: number | undefined;
  let samplingMs: number | undefined;
  let kernelMs: number | undefined;
  for (const line of output.split(/\r?\n/)) {
    let match = line.match(/^Remote: HTTP (\d+) after (\d+) ms/);
    if (match) {
      httpStatus = match[1];
      roundTripMs = parseInt(match[2]);
      continue;
    }
    match = line.match(/^Remote sampling time:\s+([\d.]+) ms/);
    if (match) {
      samplingMs = parseFloat(match[1]);
      continue;
    }
    match = line.match(/^Remote kernel time:\s+([\d.]+) ms/);
    if (match) {
      kernelMs = parseFloat(match[1]);
      continue;
    }
    match = line.match(/^(.+?):\s+(\d+) ms\s*$/);
    if (match) {
      stages.push(`${match[1]} ${formatDuration(parseInt(match[2]))}`);
    }
  }

  const result: RendererOutput = { stages: stages.join("\n") };
  if (httpStatus !== undefined) {
    const parts = [`HTTP ${httpStatus}`];
    if (roundTripMs !== undefined) parts.push(`round trip ${formatDuration(roundTripMs)}`);
    if (kernelMs !== undefined) parts.push(`kernel ${formatDuration(kernelMs)}`);
    if (samplingMs !== undefined) parts.push(`sampling ${formatDuration(samplingMs)}`);
    result.remote = parts.join(" · ");
    result.remoteDetails = parts.join("\n");
  }
  return result;
}
