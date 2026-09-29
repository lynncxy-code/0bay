import test from "node:test";
import assert from "node:assert/strict";
import { pointerForwardFromMatrix, setPicoRay } from "../src/xr-controllers.js";

test("pointerForwardFromMatrix uses WebXR -Z, not Three.js +Z", () => {
  const identity = [
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
  ];
  const forward = pointerForwardFromMatrix(identity);
  assert.ok(Math.abs(forward.x) < 1e-9);
  assert.ok(Math.abs(forward.y) < 1e-9);
  assert.ok(Math.abs(forward.z + 1) < 1e-9);
});

test("setPicoRay shows a cursor when the ray hits", () => {
  const visual = {
    userData: {
      beam: { scale: { set() {} }, material: { color: { setHex() {} }, opacity: 0 } },
      cursor: { visible: false, position: { set() {} }, material: { color: { setHex() {} } } },
    },
  };
  setPicoRay(visual, 1.2, false, true);
  assert.equal(visual.userData.cursor.visible, true);
  setPicoRay(visual, 8, false, false);
  assert.equal(visual.userData.cursor.visible, false);
});
