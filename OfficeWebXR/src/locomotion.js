export function snapTurnYaw(
  currentYaw,
  axisX,
  cooldownLeft,
  dt,
  degrees = 45,
  threshold = 0.7,
  cooldown = 0.35,
) {
  const nextCooldown = Math.max(0, cooldownLeft - dt);
  if (nextCooldown > 0 || Math.abs(axisX) <= threshold) {
    return { yaw: currentYaw, cooldown: nextCooldown, didSnap: false };
  }
  const yaw = currentYaw + Math.sign(axisX) * degrees * (Math.PI / 180);
  return { yaw, cooldown, didSnap: true };
}

export function applyStickDeadzone(x, y, deadzone = 0.12) {
  const mag = Math.hypot(x, y);
  if (mag <= deadzone) {
    return { x: 0, y: 0 };
  }
  const scaled = Math.min(1, (mag - deadzone) / (1 - deadzone));
  const k = scaled / mag;
  return { x: x * k, y: y * k };
}

export function smoothStick(prev, next, dt, rate = 16) {
  const t = 1 - Math.exp(-rate * dt);
  return {
    x: prev.x + (next.x - prev.x) * t,
    y: prev.y + (next.y - prev.y) * t,
  };
}

export function smoothTurnYaw(currentYaw, axisX, dt, speed = 2.4, deadzone = 0.18) {
  if (Math.abs(axisX) <= deadzone) {
    return currentYaw;
  }
  const mag = (Math.abs(axisX) - deadzone) / (1 - deadzone);
  return currentYaw - Math.sign(axisX) * mag * speed * dt;
}

export function moveOffset(yaw, axisX, axisY, walkSpeed, dt) {
  const forwardX = Math.sin(yaw);
  const forwardZ = Math.cos(yaw);
  const rightX = Math.cos(yaw);
  const rightZ = -Math.sin(yaw);
  return {
    x: (forwardX * axisY + rightX * axisX) * walkSpeed * dt,
    z: (forwardZ * axisY + rightZ * axisX) * walkSpeed * dt,
  };
}

export function horizontalDeltaFromLook(dirX, dirZ, axisX, axisY, walkSpeed, dt) {
  const velocity = walkVelocityFromLook(dirX, dirZ, axisX, axisY, walkSpeed);
  return { x: velocity.x * dt, z: velocity.z * dt };
}

export function walkVelocityFromLook(dirX, dirZ, axisX, axisY, walkSpeed) {
  const len = Math.hypot(dirX, dirZ) || 1;
  const fx = dirX / len;
  const fz = dirZ / len;
  const rx = -fz;
  const rz = fx;
  return {
    x: (fx * axisY + rx * axisX) * walkSpeed,
    z: (fz * axisY + rz * axisX) * walkSpeed,
  };
}

export function smoothVelocity(prev, target, dt, rate = 10) {
  const t = 1 - Math.exp(-rate * dt);
  return {
    x: prev.x + (target.x - prev.x) * t,
    z: prev.z + (target.z - prev.z) * t,
  };
}

export function teleportAimActive(rightAxisY, keyHeld) {
  return rightAxisY < -0.55 || keyHeld;
}

export function isValidFloorNormal(upComponent, minUp = 0.55) {
  return upComponent >= minUp;
}
