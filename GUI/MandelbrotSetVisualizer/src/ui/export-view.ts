// The export screen: renders the current view at a chosen size into generated-files/exports.

import { EXPORT_MAX_SIDE, EXPORT_MIN_SIDE } from "../config";
import type { MandelbrotViewer } from "../viewer";
import { byId, hideFeedback, showFeedback } from "./dom";

export function bindExportView(viewer: MandelbrotViewer): void {
  const view = byId("export-view");
  const widthInput = byId<HTMLInputElement>("input-export-width");
  const heightInput = byId<HTMLInputElement>("input-export-height");
  const preset = byId<HTMLSelectElement>("select-export-preset");
  const startButton = byId<HTMLButtonElement>("button-export-start");
  const status = byId("export-status");

  preset.addEventListener("change", () => {
    if (!preset.value) return;
    const [width, height] = preset.value.split("x");
    widthInput.value = width;
    heightInput.value = height;
  });
  // Keep the dropdown in sync when the size is typed by hand.
  const syncPreset = () => {
    const key = `${widthInput.value}x${heightInput.value}`;
    preset.value = Array.from(preset.options).some((o) => o.value === key) ? key : "";
  };
  widthInput.addEventListener("input", syncPreset);
  heightInput.addEventListener("input", syncPreset);

  byId("button-export").addEventListener("click", () => {
    hideFeedback(status);
    view.hidden = false;
  });
  byId("button-export-back").addEventListener("click", () => { view.hidden = true; });

  startButton.addEventListener("click", async () => {
    const width = parseInt(widthInput.value, 10);
    const height = parseInt(heightInput.value, 10);
    const isValidSide = (n: number) => n >= EXPORT_MIN_SIDE && n <= EXPORT_MAX_SIDE;   // false for NaN too
    if (!isValidSide(width) || !isValidSide(height)) {
      showFeedback(status, `Width and height must each be between ${EXPORT_MIN_SIDE} and ${EXPORT_MAX_SIDE}.`, "error");
      return;
    }

    startButton.disabled = true;
    startButton.textContent = "Exporting...";
    showFeedback(status, "Rendering, this can take a while for large images...", "info");
    const started = performance.now();
    try {
      const result = await viewer.exportImage({ width, height }, `export_${Date.now()}.png`);
      const seconds = ((performance.now() - started) / 1000).toFixed(1);
      showFeedback(
        status,
        `Saved ${result.path}\n${width} x ${height} px, ${result.precision}, ${seconds} s`
          + (result.warning ? `\n${result.warning}` : ""),
        result.warning ? "warning" : "ok");
    } catch (e) {
      console.error("Export failed:", e);
      showFeedback(status, `Export failed: ${e}`, "error");
    } finally {
      startButton.disabled = false;
      startButton.textContent = "Export";
    }
  });
}
