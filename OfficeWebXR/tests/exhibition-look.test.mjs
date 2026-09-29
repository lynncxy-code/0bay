import test from "node:test";
import assert from "node:assert/strict";
import { xrEyeLiftY, INTERIOR_ACES_EXPOSURE } from "../src/exhibition-look.js";

test("interior lighting stays off the factory HDRI path", () => {
  assert.ok(INTERIOR_ACES_EXPOSURE > 1);
  assert.ok(INTERIOR_ACES_EXPOSURE < 1.5);
});

test("local tracking lifts the viewer to adult standing height", () => {
  assert.ok(Math.abs(xrEyeLiftY("local", 0.05, 1.72) - 1.67) < 1e-9);
  assert.equal(xrEyeLiftY("viewer", null, 1.72), 1.72);
});

test("local-floor keeps calibrated standing height", () => {
  assert.equal(xrEyeLiftY("local-floor", 1.8, 1.72), 0);
  assert.equal(xrEyeLiftY("local-floor", null, 1.72), 0);
});

test("seated or short local-floor still reaches adult eye height", () => {
  assert.ok(Math.abs(xrEyeLiftY("local-floor", 1.2, 1.72) - 0.52) < 1e-9);
  assert.ok(Math.abs(xrEyeLiftY("local-floor", 0.4, 1.72) - 1.32) < 1e-9);
});
