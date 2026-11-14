// The state of the visualizer and what happens when it changes: which part of the plane is shown, with which
// settings, and when the renderer has to run again. It knows nothing about the page; the page listens through
// `ViewerHooks`.

import { fileUrl, generatedFilePath, runRender, type RenderArgs, type RenderCommand } from "./backend";
import { CANVAS_HEIGHT, CANVAS_WIDTH, DEFAULT_SETTINGS, MAX_UNDO_VIEWS, REMOTE_MODE, STARTING_BOUNDARY } from "./config";
import { buildExportRequest, type ExportSize } from "./export";
import { buildRecolourArgs, buildRenderRequest, type RenderInfo } from "./render-requests";
import { isColorKey, type RenderSettings } from "./render-settings";
import type { PixelBox } from "./selection";
import { ViewHistory } from "./view-history";
import { isSameViewport, zoomViewport, type Size, type Viewport } from "./viewport";
import type { StatusBar } from "./ui/status-bar";

export interface ViewerHooks {
  status: Pick<StatusBar, "renderStarted" | "recolourStarted" | "renderSucceeded" | "renderFailed">;
  // The image file has been rewritten: show it (loaded from `imageUrl`).
  imageChanged(imageUrl: string): void;
  undoAvailable(canUndo: boolean): void;
}

export type ExportResult = { path: string, precision: string, warning?: string, stages: string };

const START_VIEWPORT: Viewport = { kind: "double", boundary: { ...STARTING_BOUNDARY } };

export class MandelbrotViewer {
  private settings: RenderSettings = { ...DEFAULT_SETTINGS };
  private viewport: Viewport = START_VIEWPORT;
  private readonly history = new ViewHistory(MAX_UNDO_VIEWS);
  // The renderer keeps the escape results of the last on-screen render; they allow a recolour without iterating.
  private escapeCacheValid = false;
  private lastRenderInfo?: RenderInfo;

  private constructor(
    private readonly canvas: Size,
    private readonly imageUrl: string,
    private readonly hooks: ViewerHooks
  ) {
    this.history.onChange((canUndo) => hooks.undoAvailable(canUndo));
  }

  // Resolves where the renderer writes its image, then draws the starting view.
  public static async start(hooks: ViewerHooks, canvas: Size = { width: CANVAS_WIDTH, height: CANVAS_HEIGHT }): Promise<MandelbrotViewer> {
    const imageUrl = fileUrl(await generatedFilePath("mandelbrot_set.png"));
    const viewer = new MandelbrotViewer(canvas, imageUrl, hooks);
    void viewer.renderCurrentView();
    return viewer;
  }

  public getSettings(): Readonly<RenderSettings> {
    return this.settings;
  }

  // Changes a setting and brings the image up to date: a colour setting recolours the last render when it can,
  // anything else renders again.
  public set<K extends keyof RenderSettings>(key: K, value: RenderSettings[K]): void {
    this.settings = { ...this.settings, [key]: value };
    if (isColorKey(key) && this.escapeCacheValid) {
      void this.recolour();
    } else {
      void this.renderCurrentView();
    }
  }

  // Zooms to a box drawn on the canvas.
  public zoomTo(box: PixelBox): void {
    this.history.push(this.viewport);
    this.viewport = zoomViewport(this.viewport, box, this.canvas);
    void this.renderCurrentView();
  }

  public undo(): void {
    const previous = this.history.pop();
    if (!previous) return;
    this.viewport = previous;
    void this.renderCurrentView();
  }

  // Back to the starting view with the default iterations and palette length. The view change can be undone (unless
  // the view is already the starting one).
  public reset(): void {
    this.settings = {
      ...this.settings,
      maxIter: DEFAULT_SETTINGS.maxIter,
      paletteLength: DEFAULT_SETTINGS.paletteLength
    };
    if (!isSameViewport(this.viewport, START_VIEWPORT)) {
      this.history.push(this.viewport);
    }
    this.viewport = START_VIEWPORT;
    void this.renderCurrentView();
  }

  // Renders the current view at another size into generated-files/exports/<filename>. This does not touch the image on
  // screen or the status bar.
  public async exportImage(size: ExportSize, filename: string): Promise<ExportResult> {
    const request = buildExportRequest(this.settings, this.viewport, this.canvas, size, filename);
    const output = await runRender(request.command, request.args);
    const path = await generatedFilePath("exports", filename);
    return { path, precision: request.info.precision, warning: request.info.warning, stages: output.stages };
  }

  private renderCurrentView(): Promise<void> {
    const request = buildRenderRequest(this.settings, this.viewport, this.canvas.width);
    // A new render replaces the saved escape results, so they are only usable once it has succeeded.
    this.escapeCacheValid = false;
    this.lastRenderInfo = request.info;
    const usesRemote = this.settings.mode === REMOTE_MODE;
    this.hooks.status.renderStarted(request.info, usesRemote);
    return this.execute(request.command, request.args, request.info, { recolour: false, usesRemote });
  }

  private recolour(): Promise<void> {
    this.hooks.status.recolourStarted();
    return this.execute("recolour_mandelbrot", buildRecolourArgs(this.settings), this.lastRenderInfo!, { recolour: true, usesRemote: false });
  }

  // Runs one render and tells the page how it went (a failed render must not look like nothing happened).
  private async execute(
    command: RenderCommand, args: RenderArgs, info: RenderInfo, options: { recolour: boolean, usesRemote: boolean }
  ): Promise<void> {
    const started = performance.now();
    try {
      const output = await runRender(command, args);
      this.escapeCacheValid = true;
      this.hooks.imageChanged(this.imageUrl);
      this.hooks.status.renderSucceeded(info, output, performance.now() - started, options);
    } catch (e) {
      console.error(`Render failed (${command}, mode ${this.settings.mode}):`, e);
      this.hooks.status.renderFailed(e, options.usesRemote);
    }
  }
}
