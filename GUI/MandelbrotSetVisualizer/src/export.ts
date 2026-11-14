// Export: render the current view at any size and shape into generated-files/exports/<filename>. The centre of the
// image is the centre of the view on screen and the visible width is the same; a different shape shows more or less
// above and below.

import { DOUBLE_PRECISION_MIN_WIDTH } from "./precision";
import { buildRenderRequest, type RenderRequest } from "./render-requests";
import type { RenderSettings } from "./render-settings";
import {
  scaleHeight, scalePreciseHeight, toPreciseBoundary, type Size, type Viewport
} from "./viewport";

export type ExportSize = Size;

export function buildExportRequest(
  settings: RenderSettings, viewport: Viewport, canvas: Size, image: ExportSize, filename: string
): RenderRequest {
  // How much taller (or shorter) than the on-screen view the image is, relative to its width.
  const factor = (image.height / image.width) / (canvas.height / canvas.width);

  // A larger image has smaller pixels, so a view that is fine for a double at 900 pixels may not be at 8000.
  let exportView = viewport;
  if (viewport.kind === "double"
      && Math.abs(viewport.boundary.reEnd - viewport.boundary.reStart) < DOUBLE_PRECISION_MIN_WIDTH * image.width / canvas.width) {
    exportView = { kind: "precise", boundary: toPreciseBoundary(viewport.boundary) };
  }

  const scaled: Viewport = exportView.kind === "double"
    ? { kind: "double", boundary: scaleHeight(exportView.boundary, factor) }
    : { kind: "precise", boundary: scalePreciseHeight(exportView.boundary, factor) };

  const request = buildRenderRequest(settings, scaled, image.width);
  return {
    ...request,
    args: { ...request.args, outputFilename: filename, width: image.width, height: image.height }
  };
}
