import test from "node:test";
import assert from "node:assert/strict";
import {
  createXrRig,
  shouldWriteReferenceSpace,
  applyRigToReferenceSpace,
  clearXrRig,
  stagePositionFromPlayer,
} from "../src/xr-rig.js";

test("identity spawn does not write a reference-space offset", () => {
  const rig = createXrRig();
  assert.equal(shouldWriteReferenceSpace(rig), false);
  const written = applyRigToReferenceSpace({ xr: { setReferenceSpace() {} } }, rig);
  assert.equal(written, false);
  assert.equal(shouldWriteReferenceSpace(rig), false);
});

test("moving the rig XZ does not write a yaw-only reference space", () => {
  const rig = createXrRig();
  applyRigToReferenceSpace({ xr: { setReferenceSpace() {} } }, rig);
  rig.x = 1.5;
  assert.equal(shouldWriteReferenceSpace(rig), false);
});

test("turning the rig requires a reference-space write", () => {
  const rig = createXrRig();
  applyRigToReferenceSpace({ xr: { setReferenceSpace() {} } }, rig);
  rig.yaw = 0.4;
  assert.equal(shouldWriteReferenceSpace(rig), true);
});

test("stage slides opposite the player so stick-right is viewer-right", () => {
  const pose = stagePositionFromPlayer(1.5, -2, 1.64);
  assert.equal(pose.x, -1.5);
  assert.equal(pose.z, 2);
  assert.equal(pose.y, -1.64);
});

test("clearXrRig allows skipping identity again", () => {
  const rig = createXrRig();
  rig.x = 2;
  rig.lastX = 2;
  rig.lastZ = 0;
  rig.lastYaw = 0;
  clearXrRig(rig);
  assert.equal(shouldWriteReferenceSpace(rig), false);
});
