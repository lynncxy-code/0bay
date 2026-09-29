import test from "node:test";
import assert from "node:assert/strict";
import {
  snapTurnYaw,
  moveOffset,
  walkVelocityFromLook,
  smoothVelocity,
  teleportAimActive,
  isValidFloorNormal,
  applyStickDeadzone,
  smoothStick,
  smoothTurnYaw,
} from "../src/locomotion.js";
import { toUeThumbstick } from "../src/xr-input.js";
import { stagePositionFromPlayer } from "../src/xr-rig.js";

test("snap turn adds 45 degrees when axisX is 1 and cooldown is 0", () => {
  const result = snapTurnYaw(0, 1, 0, 0.016);
  assert.equal(result.didSnap, true);
  assert.ok(Math.abs(result.yaw - Math.PI / 4) < 1e-9);
  assert.equal(result.cooldown, 0.35);
});

test("snap turn does not fire while cooldown remains", () => {
  const result = snapTurnYaw(0, 1, 0.2, 0.016);
  assert.equal(result.didSnap, false);
  assert.equal(result.yaw, 0);
  assert.ok(Math.abs(result.cooldown - 0.184) < 1e-9);
});

test("snap turn ignores axis below 0.7", () => {
  const result = snapTurnYaw(0, 0.69, 0, 0.016);
  assert.equal(result.didSnap, false);
  assert.equal(result.yaw, 0);
});

test("snap turn negative axis subtracts 45 degrees", () => {
  const result = snapTurnYaw(0, -1, 0, 0.016);
  assert.equal(result.didSnap, true);
  assert.ok(Math.abs(result.yaw + Math.PI / 4) < 1e-9);
});

test("teleport aim is active at right stick Y -0.56", () => {
  assert.equal(teleportAimActive(-0.56, false), true);
});

test("teleport aim is inactive at right stick Y -0.54 without key", () => {
  assert.equal(teleportAimActive(-0.54, false), false);
});

test("teleport aim is active when key is held", () => {
  assert.equal(teleportAimActive(-0.54, true), true);
});

test("moveOffset at yaw 0 walks +Z and strafes +X", () => {
  const forward = moveOffset(0, 0, 1, 220, 1);
  assert.ok(Math.abs(forward.x) < 1e-9);
  assert.ok(Math.abs(forward.z - 220) < 1e-9);
  const right = moveOffset(0, 1, 0, 220, 1);
  assert.ok(Math.abs(right.x - 220) < 1e-9);
  assert.ok(Math.abs(right.z) < 1e-9);
});

test("floor normal 0.55 is valid and 0.54 is not", () => {
  assert.equal(isValidFloorNormal(0.55), true);
  assert.equal(isValidFloorNormal(0.54), false);
});

test("applyStickDeadzone zeroes noise inside the deadzone", () => {
  const idle = applyStickDeadzone(0.05, 0.05, 0.12);
  assert.equal(idle.x, 0);
  assert.equal(idle.y, 0);
});

test("applyStickDeadzone remaps full deflection to 1", () => {
  const full = applyStickDeadzone(0, 1, 0.12);
  assert.ok(Math.abs(full.x) < 1e-9);
  assert.ok(Math.abs(full.y - 1) < 1e-9);
});

test("smoothStick eases toward the target", () => {
  const next = smoothStick({ x: 0, y: 0 }, { x: 1, y: 0 }, 0.05, 16);
  assert.ok(next.x > 0 && next.x < 1);
  assert.equal(next.y, 0);
});

test("smoothTurnYaw ignores small stick values and turns at full deflection", () => {
  assert.equal(smoothTurnYaw(0, 0.1, 0.016), 0);
  const turned = smoothTurnYaw(0, 1, 1, 2.4, 0.18);
  assert.ok(turned < -2);
});

test("pushing the stick forward walks along the headset look", () => {
  const lookingMinusZ = walkVelocityFromLook(0, -1, 0, 1, 2);
  assert.ok(Math.abs(lookingMinusZ.x) < 1e-9);
  assert.ok(Math.abs(lookingMinusZ.z + 2) < 1e-9);
  const lookingPlusZ = walkVelocityFromLook(0, 1, 0, 1, 2);
  assert.ok(Math.abs(lookingPlusZ.z - 2) < 1e-9);
  const strafeLeftLookingMinusZ = walkVelocityFromLook(0, -1, -1, 0, 2);
  assert.ok(Math.abs(strafeLeftLookingMinusZ.x + 2) < 1e-9);
  const ue = toUeThumbstick({ x: -1, y: -1 });
  assert.deepEqual(ue, { x: -1, y: 1 });
  const walk = walkVelocityFromLook(0, -1, ue.x, ue.y, 1);
  const stage = stagePositionFromPlayer(walk.x, walk.z, 0);
  assert.ok(walk.x < 0);
  assert.ok(walk.z < 0);
  assert.ok(stage.x > 0);
  assert.ok(stage.z > 0);
});

test("smoothVelocity eases toward the target without overshooting", () => {
  const next = smoothVelocity({ x: 0, z: 0 }, { x: 2, z: -2 }, 0.05, 10);
  assert.ok(next.x > 0 && next.x < 2);
  assert.ok(next.z < 0 && next.z > -2);
});
