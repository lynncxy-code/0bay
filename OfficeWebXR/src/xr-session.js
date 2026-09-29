export function forceClassicXrWebGlLayer(globalObject = globalThis) {
  const proto = globalObject.XRWebGLBinding?.prototype;
  if (!proto || proto.__officeWebXrClassic) {
    return proto || null;
  }
  try {
    const desc = Object.getOwnPropertyDescriptor(proto, "createProjectionLayer");
    if (desc?.configurable) {
      delete proto.createProjectionLayer;
    }
  } catch {
    // Native prototypes may be sealed.
  }
  try {
    proto.__officeWebXrClassic = true;
  } catch {
    // ignore
  }
  return proto;
}

export function compatibleXrLayerConfigs(init = {}) {
  return [
    { antialias: false, alpha: false, depth: true, stencil: false, framebufferScaleFactor: 1 },
    { antialias: false, alpha: false, depth: true, stencil: false },
    { antialias: false, alpha: true, depth: true, stencil: false, framebufferScaleFactor: 1 },
    { ...init, antialias: false, alpha: false, stencil: false, framebufferScaleFactor: 1 },
    { antialias: false },
    {},
  ];
}

export function installCompatibleXrLayer(globalObject = globalThis) {
  const Native = globalObject.XRWebGLLayer;
  if (!Native || Native.__officeWebXrPatched) {
    return Native || null;
  }
  function CompatibleXRWebGLLayer(session, gl, init = {}) {
    let lastError;
    for (const cfg of compatibleXrLayerConfigs(init)) {
      try {
        return Reflect.construct(Native, [session, gl, cfg]);
      } catch (error) {
        lastError = error;
      }
    }
    throw lastError || new Error("XRWebGLLayer configuration is not supported");
  }
  CompatibleXRWebGLLayer.prototype = Native.prototype;
  Object.setPrototypeOf(CompatibleXRWebGLLayer, Native);
  CompatibleXRWebGLLayer.__officeWebXrPatched = true;
  globalObject.XRWebGLLayer = CompatibleXRWebGLLayer;
  return CompatibleXRWebGLLayer;
}

export function sessionHasLayers(session) {
  return !!session?.enabledFeatures?.includes("layers");
}

export function xrHasViewerPose(renderer) {
  const frame = renderer?.xr?.getFrame?.();
  const space = renderer?.xr?.getReferenceSpace?.();
  if (!frame || !space) {
    return false;
  }
  try {
    return !!frame.getViewerPose(space);
  } catch {
    return false;
  }
}

export async function diagnoseXr() {
  const ua = navigator.userAgent || "";
  if (!window.isSecureContext) {
    return { canRequest: false, reason: "not-secure", ua };
  }
  if (!navigator.xr) {
    return { canRequest: false, reason: "no-xr", ua };
  }
  let immersive = false;
  try {
    immersive = await navigator.xr.isSessionSupported("immersive-vr");
  } catch {
    immersive = false;
  }
  return {
    canRequest: true,
    ready: !!immersive,
    reason: immersive ? "ready" : "no-headset",
    ua,
  };
}

export async function isImmersiveVrSupported() {
  const info = await diagnoseXr();
  return info.ready === true;
}

export const SESSION_INITS = [
  { requiredFeatures: ["local-floor"], optionalFeatures: ["layers"] },
  { requiredFeatures: ["local-floor"] },
  { optionalFeatures: ["local-floor", "layers"] },
  { optionalFeatures: ["local-floor"] },
  {},
];

export const REFERENCE_SPACE_TYPES = ["local-floor", "bounded-floor", "local"];

export async function requestImmersiveSession(xr = navigator.xr) {
  let lastError;
  for (const init of SESSION_INITS) {
    try {
      return await xr.requestSession("immersive-vr", init);
    } catch (error) {
      lastError = error;
    }
  }
  throw lastError || new Error("immersive-vr request failed");
}

async function bindSession(renderer, session, onSpaceType) {
  if (!sessionHasLayers(session)) {
    installCompatibleXrLayer();
  }
  let lastError;
  for (const spaceType of REFERENCE_SPACE_TYPES) {
    try {
      renderer.xr.setReferenceSpaceType(spaceType);
      onSpaceType?.(spaceType);
      await renderer.xr.setSession(session);
      renderer.xr.setFoveation?.(0);
      return spaceType;
    } catch (error) {
      lastError = error;
    }
  }
  throw lastError;
}

export async function startXrSession(renderer, onSpaceType) {
  if (!navigator.xr) {
    throw new Error("WebXR not available. Use the Edge window opened by start.cmd.");
  }
  renderer.xr.enabled = true;
  renderer.xr.setFoveation?.(0);
  renderer.xr.setFramebufferScaleFactor?.(1);
  const existing = renderer.xr.getSession?.();
  if (existing) {
    try {
      await existing.end();
    } catch {
      // ignore
    }
  }
  const session = await requestImmersiveSession();
  try {
    const spaceType = await bindSession(renderer, session, onSpaceType);
    return { session, spaceType };
  } catch (error) {
    try {
      await session.end();
    } catch {
      // ignore
    }
    throw error;
  }
}

export async function endXrSession(renderer) {
  const session = renderer.xr.getSession?.();
  if (!session) {
    return;
  }
  await session.end();
}
