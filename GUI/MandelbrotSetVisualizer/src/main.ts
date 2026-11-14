import "./precision";   // sets the arbitrary-precision arithmetic up before anything uses it
import { MandelbrotViewer } from "./viewer";
import { createCanvas } from "./ui/canvas";
import { bindExportView } from "./ui/export-view";
import { bindSettingsView } from "./ui/settings-view";
import { bindSidebar, setUndoAvailable } from "./ui/sidebar";
import { StatusBar } from "./ui/status-bar";

async function main(): Promise<void> {
  const status = new StatusBar();
  let viewer: MandelbrotViewer | undefined;
  const canvas = createCanvas("main-canvas", (box) => viewer?.zoomTo(box));

  viewer = await MandelbrotViewer.start({
    status,
    imageChanged: (url) => canvas.showImage(url),
    undoAvailable: setUndoAvailable
  });

  bindSettingsView();
  bindExportView(viewer);
  await bindSidebar(viewer);
}

window.addEventListener("DOMContentLoaded", () => {
  main().catch((e) => console.error("The app failed to start:", e));
});
