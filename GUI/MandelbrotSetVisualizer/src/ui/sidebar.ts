// The sidebar: the render and colour settings, and the Undo and Reset buttons.

import { getAvailableModes } from "../backend";
import { COLOR_SPREAD_RANGE, ESCAPE_RADIUS_RANGE, MODE_LABELS } from "../config";
import type { MandelbrotViewer } from "../viewer";
import { byId, isOverlayOpen } from "./dom";

// A number field with Apply and Cancel buttons: typing only edits the field, Apply sends the value to the viewer
// and Cancel puts the applied value back. Cancel is only enabled while the field has an unapplied edit.
class ApplyField {
  private readonly input: HTMLInputElement;
  private readonly applyButton: HTMLButtonElement;
  private readonly cancelButton: HTMLButtonElement;

  // `name` is the suffix of the ids in index.html: input-<name>, button-<name>, button-<name>-cancel.
  public constructor(
    name: string,
    private readonly current: () => number,
    // The number the text stands for, or undefined when it is not usable.
    private readonly parse: (text: string) => number | undefined,
    private readonly apply: (value: number) => void
  ) {
    this.input = byId(`input-${name}`);
    this.applyButton = byId(`button-${name}`);
    this.cancelButton = byId(`button-${name}-cancel`);

    this.input.addEventListener("input", () => { this.cancelButton.disabled = false; });
    this.cancelButton.addEventListener("click", () => this.sync());
    this.applyButton.addEventListener("click", () => {
      const value = this.parse(this.input.value);
      if (value !== undefined) this.apply(value);
      this.sync();
    });
  }

  // Shows the applied value and drops any unapplied edit.
  public sync(): void {
    this.input.value = this.current().toString();
    this.cancelButton.disabled = true;
  }

  public setEnabled(enabled: boolean): void {
    this.input.disabled = !enabled;
    this.applyButton.disabled = !enabled;
    this.sync();
  }
}

const integerAtLeast = (min: number) => (text: string): number | undefined => {
  const value = parseInt(text, 10);
  return Number.isFinite(value) ? Math.max(min, value) : undefined;
};

const numberBetween = ({ min, max }: { min: number, max: number }) => (text: string): number | undefined => {
  const value = parseFloat(text);
  return Number.isFinite(value) ? Math.min(max, Math.max(min, value)) : undefined;
};

// Whether Ctrl+Z belongs to the element: in a text field it undoes typing, as usual.
function isTextEntry(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false;
  if (target instanceof HTMLTextAreaElement) return true;
  return target instanceof HTMLInputElement && target.type !== "range" && target.type !== "checkbox";
}

export function setUndoAvailable(canUndo: boolean): void {
  byId<HTMLButtonElement>("button-undo").disabled = !canUndo;
}

export async function bindSidebar(viewer: MandelbrotViewer): Promise<void> {
  const settings = () => viewer.getSettings();

  const maxIter = new ApplyField("max-iter", () => settings().maxIter, integerAtLeast(1), (v) => viewer.set("maxIter", v));
  const samples = new ApplyField("samples", () => settings().samples, integerAtLeast(1), (v) => viewer.set("samples", v));
  const escapeRadius = new ApplyField("escape-radius", () => settings().escapeRadius,
    numberBetween(ESCAPE_RADIUS_RANGE), (v) => viewer.set("escapeRadius", v));
  const paletteLength = new ApplyField("palette-length", () => settings().paletteLength,
    integerAtLeast(1), (v) => viewer.set("paletteLength", v));
  const colorSpread = new ApplyField("color-spread", () => settings().colorSpread,
    numberBetween(COLOR_SPREAD_RANGE), (v) => viewer.set("colorSpread", v));
  const fields = [maxIter, samples, escapeRadius, paletteLength, colorSpread];

  byId("select-color-palette").addEventListener("change", (e) => {
    viewer.set("paletteId", parseInt((e.target as HTMLSelectElement).value, 10));
  });
  byId("input-color-phase").addEventListener("change", (e) => {
    viewer.set("colorPhase", parseFloat((e.target as HTMLInputElement).value));
  });
  const histogramCycles = byId<HTMLInputElement>("input-histogram-cycles");
  histogramCycles.addEventListener("change", () => viewer.set("histogramCycles", parseFloat(histogramCycles.value)));

  // Histogram colouring ignores the palette length, escape radius and colour spread, and only it uses the cycles.
  byId("input-histogram").addEventListener("change", (e) => {
    const on = (e.target as HTMLInputElement).checked;
    for (const field of [paletteLength, escapeRadius, colorSpread]) {
      field.setEnabled(!on);
    }
    histogramCycles.disabled = !on;
    viewer.set("histogram", on);
  });

  byId("button-undo").addEventListener("click", () => viewer.undo());
  document.addEventListener("keydown", (e) => {
    const isUndoShortcut = (e.ctrlKey || e.metaKey) && !e.shiftKey && !e.altKey && e.key.toLowerCase() === "z";
    if (!isUndoShortcut || isOverlayOpen() || isTextEntry(e.target)) return;
    e.preventDefault();
    viewer.undo();
  });
  byId("button-reset").addEventListener("click", () => {
    viewer.reset();
    fields.forEach((field) => field.sync());
  });

  await fillModes(viewer);
}

// The mode list shows the modes this machine can render with.
async function fillModes(viewer: MandelbrotViewer): Promise<void> {
  const select = byId<HTMLSelectElement>("select-mode");
  select.addEventListener("change", () => viewer.set("mode", select.value));
  try {
    const modes = await getAvailableModes();
    select.replaceChildren(...modes.map((mode) => {
      const option = document.createElement("option");
      option.value = mode;
      option.textContent = MODE_LABELS[mode] ?? mode;
      return option;
    }));
    select.value = viewer.getSettings().mode;
  } catch (e) {
    console.error("Failed to detect available modes:", e);
  }
}
