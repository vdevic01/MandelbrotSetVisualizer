// The element with this id. A missing element is a mistake in index.html, so this fails loudly instead of letting
// a control silently do nothing.
export function byId<T extends HTMLElement = HTMLElement>(id: string): T {
  const element = document.getElementById(id);
  if (!element) {
    throw new Error(`index.html has no element with id "${id}"`);
  }
  return element as T;
}

// A line of feedback below a form: grey for progress, green when it worked, yellow for a warning, red for an error.
export type FeedbackKind = "info" | "ok" | "warning" | "error";

export function showFeedback(element: HTMLElement, text: string, kind: FeedbackKind): void {
  element.textContent = text;
  element.dataset.kind = kind;
  element.hidden = false;
}

export function hideFeedback(element: HTMLElement): void {
  element.hidden = true;
}

// True while the settings or the export screen covers the canvas.
export function isOverlayOpen(): boolean {
  return document.querySelector(".overlay-view:not([hidden])") !== null;
}
