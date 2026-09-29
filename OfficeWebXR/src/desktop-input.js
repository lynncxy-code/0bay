export function createDesktopInput(domElement) {
  const keys = new Set();
  const look = { x: 0, y: 0 };
  let pointerLocked = false;

  function onKeyDown(event) {
    keys.add(event.code);
  }

  function onKeyUp(event) {
    keys.delete(event.code);
  }

  function onMouseMove(event) {
    if (!pointerLocked) {
      return;
    }
    look.x += event.movementX || 0;
    look.y += event.movementY || 0;
  }

  function onClick() {
    if (!pointerLocked) {
      domElement.requestPointerLock?.();
    }
  }

  function onPointerLockChange() {
    pointerLocked = document.pointerLockElement === domElement;
  }

  window.addEventListener("keydown", onKeyDown);
  window.addEventListener("keyup", onKeyUp);
  window.addEventListener("mousemove", onMouseMove);
  domElement.addEventListener("click", onClick);
  document.addEventListener("pointerlockchange", onPointerLockChange);

  return {
    keys,
    isPointerLocked() {
      return pointerLocked;
    },
    consumeLook() {
      const delta = { x: look.x, y: look.y };
      look.x = 0;
      look.y = 0;
      return delta;
    },
    axisFromKeys() {
      let x = 0;
      let y = 0;
      if (keys.has("KeyW") || keys.has("ArrowUp")) y += 1;
      if (keys.has("KeyS") || keys.has("ArrowDown")) y -= 1;
      if (keys.has("KeyD") || keys.has("ArrowRight")) x += 1;
      if (keys.has("KeyA") || keys.has("ArrowLeft")) x -= 1;
      return { x, y };
    },
    snapFromKeys() {
      if (keys.has("KeyE")) return 1;
      if (keys.has("KeyQ")) return -1;
      return 0;
    },
    teleportHeld() {
      return keys.has("KeyT");
    },
    dispose() {
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
      window.removeEventListener("mousemove", onMouseMove);
      domElement.removeEventListener("click", onClick);
      document.removeEventListener("pointerlockchange", onPointerLockChange);
    },
  };
}
