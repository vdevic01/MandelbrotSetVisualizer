// The zoom box the user drags on the canvas. It always has the shape of the canvas, so zooming does not distort the
// image: the corner under the pointer is moved until the box fits.

export type Point = { x: number, y: number };

// Pixel edges of a box (top is the smaller y).
export type PixelBox = { left: number, right: number, top: number, bottom: number };

// The corner opposite to `press` of the box that has the given width / height `aspect` and spans the pointer's
// bounding box on its longer side.
export function fitSelection(press: Point, pointer: Point, aspect: number): Point {
  const width = pointer.x - press.x;
  const height = pointer.y - press.y;
  if (Math.abs(width) > Math.abs(height) * aspect) {
    return { x: pointer.x, y: press.y + Math.sign(height) * Math.abs(width) / aspect };
  }
  return { x: press.x + Math.sign(width) * Math.abs(height) * aspect, y: pointer.y };
}

export function toPixelBox(a: Point, b: Point): PixelBox {
  return {
    left: Math.min(a.x, b.x),
    right: Math.max(a.x, b.x),
    top: Math.min(a.y, b.y),
    bottom: Math.max(a.y, b.y)
  };
}
