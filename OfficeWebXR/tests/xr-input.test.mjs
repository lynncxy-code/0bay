import test from "node:test";
import assert from "node:assert/strict";
import {
  readThumbstick,
  isTriggerClicked,
  isBClicked,
  toUeThumbstick,
  gamepadFromSource,
} from "../src/xr-input.js";

function pad(axes, buttons = []) {
  return { axes, buttons };
}

test("readThumbstick uses axes 2 and 3 when four axes are present", () => {
  const stick = readThumbstick(pad([0, 0, 0.8, -0.9]));
  assert.equal(stick.x, 0.8);
  assert.equal(stick.y, -0.9);
});

test("readThumbstick falls back to axes 0 and 1 when only two axes exist", () => {
  const stick = readThumbstick(pad([0.8, -0.9]));
  assert.equal(stick.x, 0.8);
  assert.equal(stick.y, -0.9);
});

test("readThumbstick uses axes 0 and 1 when 2 and 3 are idle", () => {
  const stick = readThumbstick(pad([0.8, -0.9, 0, 0]));
  assert.equal(stick.x, 0.8);
  assert.equal(stick.y, -0.9);
});

test("toUeThumbstick inverts Y so WebXR stick-up becomes forward", () => {
  assert.deepEqual(toUeThumbstick({ x: 0.2, y: -1 }), { x: 0.2, y: 1 });
});

test("trigger fires only on rising edge", () => {
  const released = pad([], [{ pressed: false }]);
  const pressed = pad([], [{ pressed: true }]);
  assert.equal(isTriggerClicked(released, pressed), true);
  assert.equal(isTriggerClicked(pressed, pressed), false);
  assert.equal(isTriggerClicked(pressed, released), false);
});

test("gamepadFromSource prefers the XR source pad", () => {
  const pad = { axes: [1], buttons: [] };
  assert.equal(gamepadFromSource({ gamepad: pad }, []), pad);
});

test("gamepadFromSource falls back to navigator pads", () => {
  const fallbackPad = { axes: [0.5], buttons: [] };
  assert.equal(gamepadFromSource({}, [null, fallbackPad], 1), fallbackPad);
});

test("B fires only on rising edge of button 5", () => {
  const buttons = (pressed) => [
    { pressed: false },
    { pressed: false },
    { pressed: false },
    { pressed: false },
    { pressed: false },
    { pressed },
  ];
  const released = pad([], buttons(false));
  const down = pad([], buttons(true));
  assert.equal(isBClicked(released, down), true);
  assert.equal(isBClicked(down, down), false);
});
