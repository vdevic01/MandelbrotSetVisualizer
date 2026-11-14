import type { Viewport } from "./viewport";

// The views before the current one, for undo. Only the view is stored; iterations, palette and the other settings
// stay as they are when going back.
export class ViewHistory {
  private readonly views: Viewport[] = [];
  private listener?: (canUndo: boolean) => void;

  public constructor(private readonly capacity: number) {}

  public get canUndo(): boolean {
    return this.views.length > 0;
  }

  // Tells the caller (the Undo button) whenever undo becomes possible or impossible, and right now.
  public onChange(listener: (canUndo: boolean) => void): void {
    this.listener = listener;
    listener(this.canUndo);
  }

  // Call before the view changes.
  public push(view: Viewport): void {
    this.views.push(view);
    if (this.views.length > this.capacity) {
      this.views.shift();
    }
    this.listener?.(true);
  }

  public pop(): Viewport | undefined {
    const view = this.views.pop();
    this.listener?.(this.canUndo);
    return view;
  }
}
