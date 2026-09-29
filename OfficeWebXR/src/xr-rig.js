export function createXrRig() {
  return {
    baseSpace: null,
    x: 0,
    z: 0,
    yaw: 0,
    eyeLift: 0,
    spaceType: null,
    eyeLocked: false,
    lastX: undefined,
    lastZ: undefined,
    lastYaw: undefined,
  };
}

export function stagePositionFromPlayer(x, z, eyeLift = 0) {
  return { x: -x, y: -eyeLift, z: -z };
}

export function applyStageFromPlayer(stage, rig) {
  const pose = stagePositionFromPlayer(rig.x, rig.z, rig.eyeLift || 0);
  stage.position.set(pose.x, pose.y, pose.z);
  return pose;
}

export function captureBaseSpace(renderer, rig) {
  const space = renderer.xr.getReferenceSpace?.();
  if (space && !rig.baseSpace) {
    rig.baseSpace = space;
  }
}

export function clearXrRig(rig) {
  rig.baseSpace = null;
  rig.x = 0;
  rig.z = 0;
  rig.yaw = 0;
  rig.eyeLift = 0;
  rig.spaceType = null;
  rig.eyeLocked = false;
  rig.lastX = undefined;
  rig.lastZ = undefined;
  rig.lastYaw = undefined;
}

export function moveXrRig(rig, dx, dz, yawDelta) {
  rig.x += dx;
  rig.z += dz;
  rig.yaw += yawDelta;
}

export function setXrRigPose(rig, x, z, yaw) {
  rig.x = x;
  rig.z = z;
  if (yaw != null) {
    rig.yaw = yaw;
  }
}

export function shouldWriteReferenceSpace(rig) {
  if (rig.lastYaw === rig.yaw) {
    return false;
  }
  if (rig.yaw === 0 && rig.lastYaw === undefined) {
    return false;
  }
  return true;
}

function markRigWritten(rig) {
  rig.lastX = rig.x;
  rig.lastZ = rig.z;
  rig.lastYaw = rig.yaw;
}

export function yawOrientation(yaw) {
  const half = -yaw * 0.5;
  return { x: 0, y: Math.sin(half), z: 0, w: Math.cos(half) };
}

export function applyRigToReferenceSpace(renderer, rig) {
  if (!shouldWriteReferenceSpace(rig)) {
    if (rig.lastYaw === undefined) {
      markRigWritten(rig);
    }
    return false;
  }
  if (!rig.baseSpace || typeof XRRigidTransform === "undefined") {
    return false;
  }
  const transform = new XRRigidTransform(
    { x: 0, y: 0, z: 0 },
    yawOrientation(rig.yaw),
  );
  renderer.xr.setReferenceSpace(rig.baseSpace.getOffsetReferenceSpace(transform));
  markRigWritten(rig);
  return true;
}
