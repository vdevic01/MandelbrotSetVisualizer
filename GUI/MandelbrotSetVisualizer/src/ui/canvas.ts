// The canvas: shows the rendered image and lets the user drag a zoom box over it.

import P5 from "p5";
import { CANVAS_HEIGHT, CANVAS_WIDTH, MIN_SELECTION_PX } from "../config";
import { fitSelection, toPixelBox, type PixelBox, type Point } from "../selection";
import { isOverlayOpen } from "./dom";

const BACKGROUND_COLOR: [number, number, number] = [71, 71, 78];

export type Canvas = {
  // Shows the image at this URL as soon as it has loaded; until then the previous image stays.
  showImage(url: string): void
};

export function createCanvas(parentId: string, onZoom: (box: PixelBox) => void): Canvas {
  let image: P5.Image | undefined;
  let press: Point | undefined;   // where the current drag started
  const aspect = CANVAS_WIDTH / CANVAS_HEIGHT;

  const sketch = (p5: P5) => {
    const pointer = (): Point => ({ x: p5.mouseX, y: p5.mouseY });
    const isInside = (p: Point) => p.x >= 0 && p.x <= p5.width && p.y >= 0 && p.y <= p5.height;

    p5.setup = () => {
      p5.createCanvas(CANVAS_WIDTH, CANVAS_HEIGHT).parent(parentId);
    };

    p5.draw = () => {
      if (image) {
        p5.image(image, 0, 0);
      } else {
        p5.background(...BACKGROUND_COLOR);
      }
      if (press) {
        const end = fitSelection(press, pointer(), aspect);
        p5.noFill();
        p5.stroke(255);
        p5.strokeWeight(4);
        p5.rect(press.x, press.y, end.x - press.x, end.y - press.y);
        p5.stroke(0);
        p5.strokeWeight(2);
        p5.rect(press.x, press.y, end.x - press.x, end.y - press.y);
      }
    };

    p5.mousePressed = () => {
      if (isOverlayOpen() || p5.mouseButton !== p5.LEFT || !isInside(pointer())) return;
      press = pointer();
    };

    p5.mouseReleased = () => {
      const start = press;
      press = undefined;
      if (!start || isOverlayOpen()) return;
      const end = fitSelection(start, pointer(), aspect);
      const box = toPixelBox(start, end);
      // A click without a drag is not a zoom (it would select an empty box).
      if (box.right - box.left < MIN_SELECTION_PX || box.bottom - box.top < MIN_SELECTION_PX) return;
      onZoom(box);
    };
  };

  const p5Instance = new P5(sketch);
  return {
    showImage(url: string) {
      p5Instance.loadImage(url, (loaded) => { image = loaded; });
    }
  };
}
