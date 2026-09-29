import test from "node:test";
import assert from "node:assert/strict";
import {
  forceClassicXrWebGlLayer,
  installCompatibleXrLayer,
  compatibleXrLayerConfigs,
  requestImmersiveSession,
  xrHasViewerPose,
  SESSION_INITS,
} from "../src/xr-session.js";
import {
  NEEDLE,
  REPLACEMENT,
  LAYER_NEEDLE,
  LAYER_REPLACEMENT,
  patchThreeWebXrSource,
} from "../scripts/patch-three-webxr.mjs";

test("forceClassicXrWebGlLayer deletes a configurable createProjectionLayer", () => {
  function FakeBinding() {}
  FakeBinding.prototype.createProjectionLayer = function createProjectionLayer() {
    return { kind: "projection" };
  };
  const proto = forceClassicXrWebGlLayer({ XRWebGLBinding: FakeBinding });
  assert.equal("createProjectionLayer" in proto, false);
  assert.equal(proto.__officeWebXrClassic, true);
});

test("forceClassicXrWebGlLayer is idempotent", () => {
  function FakeBinding() {}
  const globalObject = { XRWebGLBinding: FakeBinding };
  const first = forceClassicXrWebGlLayer(globalObject);
  const second = forceClassicXrWebGlLayer(globalObject);
  assert.equal(first, second);
});

test("patchThreeWebXrSource only uses XRProjectionLayer when the session enabled layers", () => {
  const patched = patchThreeWebXrSource(NEEDLE);
  assert.equal(patched.changed, true);
  assert.equal(patched.source, REPLACEMENT);
});

test("patchThreeWebXrSource uses SteamVR-safe XRWebGLLayer init", () => {
  const patched = patchThreeWebXrSource(LAYER_NEEDLE);
  assert.equal(patched.changed, true);
  assert.equal(patched.source, LAYER_REPLACEMENT);
  assert.match(patched.source, /antialias: false/);
  assert.match(patched.source, /alpha: false/);
});

test("installCompatibleXrLayer retries until a supported config works", () => {
  function NativeLayer(session, gl, init = {}) {
    if (init.antialias || init.alpha || init.stencil) {
      throw new Error("The specified configuration is not supported");
    }
    this.init = init;
  }
  const globalObject = { XRWebGLLayer: NativeLayer };
  installCompatibleXrLayer(globalObject);
  const layer = new globalObject.XRWebGLLayer({}, {}, { antialias: true, alpha: true, stencil: true });
  assert.equal(layer.init.antialias, false);
  assert.equal(layer.init.alpha, false);
});

test("compatibleXrLayerConfigs always tries a no-feature fallback", () => {
  const configs = compatibleXrLayerConfigs({ antialias: true });
  assert.equal(configs.at(-1) && Object.keys(configs.at(-1)).length, 0);
});

test("requestImmersiveSession falls back to a bare immersive-vr request", async () => {
  const calls = [];
  const xr = {
    async requestSession(mode, init) {
      calls.push({ mode, init });
      if (init?.optionalFeatures || init?.requiredFeatures) {
        throw new Error("The specified configuration is not supported");
      }
      return { id: "ok" };
    },
  };
  const session = await requestImmersiveSession(xr);
  assert.equal(session.id, "ok");
  assert.equal(calls.length, 5);
});

test("first immersive session request requires local-floor", () => {
  assert.deepEqual(SESSION_INITS[0].requiredFeatures, ["local-floor"]);
});

test("xrHasViewerPose is false without a frame", () => {
  assert.equal(xrHasViewerPose({ xr: { getFrame() { return null; } } }), false);
});
