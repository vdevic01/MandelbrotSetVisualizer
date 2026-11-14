// The commands of the Rust backend (src-tauri/src/main.rs), typed in one place.

import { invoke, convertFileSrc } from "@tauri-apps/api/tauri";
import { join } from "@tauri-apps/api/path";
import { parseRendererOutput, type RendererOutput } from "./renderer-output";

export type RenderCommand = "generate_mandelbrot" | "generate_mandelbrot_hp" | "recolour_mandelbrot";
export type RenderArgs = Record<string, unknown>;

export type RemoteSettings = { endpoint: string, api_key: string };

// Runs the renderer and reads its report. Throws when the render fails or the report cannot be read.
export async function runRender(command: RenderCommand, args: RenderArgs): Promise<RendererOutput> {
  return parseRendererOutput(await invoke<string>(command, args));
}

export function getAvailableModes(): Promise<string[]> {
  return invoke("get_available_modes");
}

export function getRemoteSettings(): Promise<RemoteSettings> {
  return invoke("get_remote_settings");
}

export function saveRemoteSettings(endpoint: string, apiKey: string): Promise<void> {
  return invoke("save_remote_settings", { endpoint, apiKey });
}

// Absolute path of a file below the backend's generated-files folder.
export async function generatedFilePath(...segments: string[]): Promise<string> {
  const projectDir = await invoke<string>("get_project_dir");
  return join(projectDir, "generated-files", ...segments);
}

// The URL under which the webview can load a file of the backend.
export function fileUrl(path: string): string {
  return convertFileSrc(path);
}
