import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

export const NEEDLE = "const supportsLayers = supportsGlBinding && 'createProjectionLayer' in XRWebGLBinding.prototype;";
export const REPLACEMENT = "const supportsLayers = supportsGlBinding && !!session.enabledFeatures?.includes( 'layers' ) && 'createProjectionLayer' in XRWebGLBinding.prototype;";

export const LAYER_NEEDLE = `					const layerInit = {
						antialias: attributes.antialias,
						alpha: true,
						depth: attributes.depth,
						stencil: attributes.stencil,
						framebufferScaleFactor: framebufferScaleFactor
					};`;

export const LAYER_REPLACEMENT = `					const layerInit = {
						antialias: false,
						alpha: false,
						depth: true,
						stencil: false,
						framebufferScaleFactor: 1.0
					};`;

export function patchThreeWebXrSource(source) {
  let next = source;
  let changed = false;
  if (next.includes(NEEDLE)) {
    next = next.replaceAll(NEEDLE, REPLACEMENT);
    changed = true;
  }
  if (next.includes(LAYER_NEEDLE)) {
    next = next.replaceAll(LAYER_NEEDLE, LAYER_REPLACEMENT);
    changed = true;
  }
  return { source: next, changed };
}

export function patchThreeWebXrFiles(root = join(dirname(fileURLToPath(import.meta.url)), "..")) {
  const files = [
    join(root, "node_modules", "three", "build", "three.module.js"),
    join(root, "node_modules", "three", "src", "renderers", "webxr", "WebXRManager.js"),
  ];
  const patched = [];
  for (const file of files) {
    if (!existsSync(file)) {
      continue;
    }
    const result = patchThreeWebXrSource(readFileSync(file, "utf8"));
    if (result.changed) {
      writeFileSync(file, result.source);
      patched.push(file);
    }
  }
  return patched;
}

if (process.argv[1] && process.argv[1].replaceAll("\\", "/").endsWith("patch-three-webxr.mjs")) {
  const patched = patchThreeWebXrFiles();
  console.log(patched.length ? `Patched three.js WebXR layer check:\n${patched.join("\n")}` : "three.js WebXR layer check already patched.");
}
