export function readThumbstick(gamepad) {
  if (!gamepad || !gamepad.axes) {
    return { x: 0, y: 0 };
  }
  const axes = gamepad.axes;
  if (axes.length >= 4) {
    const fallback = { x: axes[0] || 0, y: axes[1] || 0 };
    const alt = { x: axes[2] || 0, y: axes[3] || 0 };
    if (Math.abs(alt.x) + Math.abs(alt.y) >= Math.abs(fallback.x) + Math.abs(fallback.y)) {
      return alt;
    }
    return fallback;
  }
  return { x: axes[0] || 0, y: axes[1] || 0 };
}

export function toUeThumbstick(stick) {
  return { x: stick.x, y: -stick.y };
}

function buttonPressed(pad, index) {
  return pad?.buttons?.[index]?.pressed === true;
}

export function isTriggerClicked(prev, next) {
  return !buttonPressed(prev, 0) && buttonPressed(next, 0);
}

export function isBClicked(prev, next) {
  return (!buttonPressed(prev, 5) && buttonPressed(next, 5))
    || (!buttonPressed(prev, 4) && buttonPressed(next, 4));
}

export function gamepadFromSource(source, pads = [], fallbackIndex = 0) {
  if (source?.gamepad) {
    return source.gamepad;
  }
  if (pads[fallbackIndex]) {
    return pads[fallbackIndex];
  }
  return (pads || []).filter(Boolean)[0] || null;
}

export function snapshotGamepad(gamepad) {
  if (!gamepad) {
    return null;
  }
  return {
    axes: Array.from(gamepad.axes || []),
    buttons: (gamepad.buttons || []).map((button) => ({ pressed: !!button.pressed })),
  };
}

export function controllersByHand(session, renderer, pads = []) {
  const result = { left: null, right: null };
  if (!session || !renderer?.xr) {
    return result;
  }
  const sources = session.inputSources || [];
  let unnamed = 0;
  for (let index = 0; index < sources.length; index += 1) {
    const source = sources[index];
    const hand = source.handedness === "left" || source.handedness === "right"
      ? source.handedness
      : null;
    const fallbackIndex = hand === "right" ? 1 : unnamed;
    if (!hand) {
      unnamed += 1;
      continue;
    }
    result[hand] = {
      source,
      object: renderer.xr.getController(index),
      gamepad: gamepadFromSource(source, pads, fallbackIndex),
    };
  }
  if (!result.left && pads[0]) {
    result.left = { source: null, object: renderer.xr.getController(0), gamepad: pads[0] };
  }
  if (!result.right && pads[1]) {
    result.right = { source: null, object: renderer.xr.getController(1), gamepad: pads[1] };
  }
  return result;
}
