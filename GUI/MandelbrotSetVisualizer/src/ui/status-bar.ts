// Status bar above the canvas: the precision of the last render, the scale of the view, how long the render took,
// what the remote worker answered, and a message line for warnings and errors.

import { formatDuration, type RendererOutput } from "../renderer-output";
import type { RenderInfo } from "../render-requests";
import { byId } from "./dom";

type MessageKind = "info" | "warning" | "error";

export class StatusBar {
  private readonly precision = byId("status-precision");
  private readonly scale = byId("status-scale");
  private readonly time = byId("status-time");
  private readonly remote = byId("status-remote");
  private readonly message = byId("status-message");

  // A full render starts. `usesRemote` is true when the remote worker is going to answer.
  public renderStarted(info: RenderInfo, usesRemote: boolean): void {
    this.setField(this.precision, info.precision);
    this.setScale(info.scaleLog10);
    this.setField(this.remote, usesRemote ? "waiting for the worker..." : "not used (local render)");
    this.setField(this.time, "rendering...");
    this.setMessage("");
  }

  public recolourStarted(): void {
    this.setField(this.time, "recolouring...");
    this.setMessage("");
  }

  public renderSucceeded(
    info: RenderInfo, output: RendererOutput, elapsedMs: number, options: { recolour: boolean, usesRemote: boolean }
  ): void {
    this.setField(this.time, formatDuration(elapsedMs) + (options.recolour ? " (recoloured)" : ""), output.stages);
    if (options.usesRemote) {
      this.setField(this.remote, output.remote ?? "no information from the worker", output.remoteDetails);
    }
    if (info.warning) {
      this.setMessage(info.warning, "warning");
    } else if (output.warnings.length > 0) {
      this.setMessage(output.warnings.join("\n"), "warning");
    }
  }

  public renderFailed(error: unknown, usesRemote: boolean): void {
    this.setField(this.time, "failed");
    if (usesRemote) {
      this.setField(this.remote, "failed (see below)");
    }
    this.setMessage(String(error), "error");
  }

  private setField(element: HTMLElement, text: string, tooltip: string = ""): void {
    element.textContent = text;
    element.title = tooltip || text;
  }

  // Shows the order of magnitude of the width of the visible region, e.g. 10^-13.2.
  private setScale(log10Width: number): void {
    const exponent = log10Width.toFixed(1);
    const sup = document.createElement("sup");
    sup.textContent = exponent;
    this.scale.replaceChildren("10", sup);
    this.scale.title = `The visible region of the complex plane is about 10^${exponent} wide (the whole set is about 10^0.5)`;
  }

  // One line in the bar; the full text (an error can be several lines) is in the tooltip.
  private setMessage(message: string, kind: MessageKind = "info"): void {
    this.message.textContent = message.split("\n")[0];
    this.message.title = message;
    this.message.classList.toggle("warning", kind === "warning");
    this.message.classList.toggle("error", kind === "error");
  }
}
