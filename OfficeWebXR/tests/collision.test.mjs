import test from "node:test";
import assert from "node:assert/strict";
import { resolveHorizontalMove, classifyCollisionKind } from "../src/collision.js";

class Vector3 {
  constructor(x = 0, y = 0, z = 0) {
    this.x = x;
    this.y = y;
    this.z = z;
  }
  set(x, y, z) {
    this.x = x;
    this.y = y;
    this.z = z;
    return this;
  }
}

const THREE = { Vector3 };

function raycasterWithHits(chooseHit) {
  return {
    far: 0,
    _direction: new Vector3(),
    set(_origin, direction) {
      this._direction = direction;
    },
    intersectObjects(objects) {
      const hit = chooseHit(this._direction, objects);
      return hit ? [hit] : [];
    },
  };
}

test("resolveHorizontalMove with no walls applies the offset", () => {
  const next = resolveHorizontalMove(
    raycasterWithHits(() => null),
    { x: 0, y: 0, z: 0 },
    1,
    2,
    [],
    0.34,
    THREE,
  );
  assert.equal(next.x, 1);
  assert.equal(next.z, 2);
});

test("resolveHorizontalMove blocks a wall and slides on the free axis", () => {
  const wall = { userData: { wall: true } };
  const raycaster = raycasterWithHits((direction) => {
    if (Math.abs(direction.x) >= 0.5) {
      return { distance: 0.02, object: wall };
    }
    return null;
  });
  const next = resolveHorizontalMove(
    raycaster,
    { x: 0, y: 0, z: 0 },
    1,
    1,
    [wall],
    0.34,
    THREE,
  );
  assert.equal(next.x, 0);
  assert.equal(next.z, 1);
});

test("classifyCollisionKind treats office shell floors and ceilings as floors", () => {
  assert.equal(classifyCollisionKind("Floor Office Shell"), "floor");
  assert.equal(classifyCollisionKind("RoofSlab Office Shell"), "floor");
  assert.equal(classifyCollisionKind("CeilingMain Office Shell"), "floor");
});

test("classifyCollisionKind treats glass partitions and doors as walls", () => {
  assert.equal(classifyCollisionKind("Walls Office Shell"), "wall");
  assert.equal(classifyCollisionKind("Columns Office Shell"), "wall");
  assert.equal(classifyCollisionKind("InteriorGlass Office Shell"), "wall");
  assert.equal(classifyCollisionKind("ExteriorGlass Office Shell"), "wall");
  assert.equal(classifyCollisionKind("DoorLeaves Office Shell"), "wall");
  assert.equal(classifyCollisionKind("CorePerimeter Office Shell"), "wall");
});

test("classifyCollisionKind ignores furniture and city meshes", () => {
  assert.equal(classifyCollisionKind("PDF_07_会议桌_大会议室"), null);
  assert.equal(classifyCollisionKind("ZeroBay Buildings"), null);
});
