// The settings screen: endpoint and API key of the remote CUDA worker.

import { getRemoteSettings, saveRemoteSettings } from "../backend";
import { byId, hideFeedback, showFeedback } from "./dom";

const SAVED_MESSAGE_MS = 2000;

export function bindSettingsView(): void {
  const view = byId("settings-view");
  const endpoint = byId<HTMLInputElement>("input-remote-endpoint");
  const apiKey = byId<HTMLInputElement>("input-remote-api-key");
  const status = byId("remote-save-status");

  byId("button-settings").addEventListener("click", async () => {
    try {
      const saved = await getRemoteSettings();
      endpoint.value = saved.endpoint ?? "";
      apiKey.value = saved.api_key ?? "";
    } catch (e) {
      console.error("Failed to load settings:", e);
    }
    hideFeedback(status);
    view.hidden = false;
  });
  byId("button-settings-back").addEventListener("click", () => { view.hidden = true; });

  byId("button-remote-save").addEventListener("click", async () => {
    try {
      await saveRemoteSettings(endpoint.value.trim(), apiKey.value.trim());
      showFeedback(status, "Saved.", "ok");
      setTimeout(() => hideFeedback(status), SAVED_MESSAGE_MS);
    } catch (e) {
      showFeedback(status, `Failed to save: ${e}`, "error");
    }
  });
}
