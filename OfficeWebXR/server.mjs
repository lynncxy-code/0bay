import { createReadStream, existsSync } from "node:fs";
import { stat } from "node:fs/promises";
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";
import { extname, join, normalize, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { ensureSteamVr, freePort } from "./scripts/ensure-steamvr.mjs";

const host = process.env.HOST || "127.0.0.1";
const port = Number(process.env.PORT || 4193);
const root = resolve(fileURLToPath(new URL(".", import.meta.url)));
const dataRoot = resolve(process.env.DATA_ROOT || join(root, "data"));
const contentTypes = {
  ".css": "text/css; charset=utf-8",
  ".bin": "application/octet-stream",
  ".glb": "model/gltf-binary",
  ".gltf": "model/gltf+json",
  ".hdr": "application/octet-stream",
  ".html": "text/html; charset=utf-8",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".png": "image/png",
  ".svg": "image/svg+xml",
  ".wasm": "application/wasm",
};

const server = createServer(async (request, response) => {
  let requestPath;
  try {
    requestPath = decodeURIComponent(new URL(request.url, `http://${host}:${port}`).pathname);
  } catch {
    response.writeHead(400, { "Content-Type": "text/plain; charset=utf-8" });
    response.end("Invalid URL encoding");
    return;
  }
  if (requestPath === "/") {
    requestPath = "/office.html";
  }
  const servesData = requestPath === "/data" || requestPath.startsWith("/data/");
  const sourceRoot = servesData ? dataRoot : root;
  const relativePath = servesData
    ? requestPath.replace(/^\/data\/?/, "")
    : requestPath.slice(1);
  const safePath = normalize(relativePath).replace(/^(\.\.(\/|\\|$))+/, "");
  const filePath = resolve(sourceRoot, safePath);
  try {
    if (filePath !== sourceRoot && !filePath.startsWith(`${sourceRoot}${sep}`)) {
      throw new Error("Path outside content root");
    }
    const fileStat = await stat(filePath);
    if (!fileStat.isFile()) throw new Error("Not a file");
    const extension = extname(filePath).toLowerCase();
    const contentType = contentTypes[extension] || "application/octet-stream";
    response.writeHead(200, {
      "Cache-Control": "no-store",
      "Content-Length": fileStat.size,
      "Content-Type": contentType,
    });
    if (request.method === "HEAD") {
      response.end();
      return;
    }
    const stream = createReadStream(filePath);
    stream.on("error", () => {
      stream.destroy();
      if (!response.writableEnded) {
        response.end();
      }
    });
    response.on("close", () => stream.destroy());
    stream.pipe(response);
  } catch {
    if (response.headersSent) {
      response.end();
      return;
    }
    response.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    response.end("Not found");
  }
});

const pageUrl = `http://${host}:${port}/office.html`;

function findChromium() {
  const candidates = [
    join(process.env.PROGRAMFILES || "", "Microsoft", "Edge", "Application", "msedge.exe"),
    join(process.env["PROGRAMFILES(X86)"] || "", "Microsoft", "Edge", "Application", "msedge.exe"),
    join(process.env.LOCALAPPDATA || "", "Microsoft", "Edge", "Application", "msedge.exe"),
    join(process.env.PROGRAMFILES || "", "Google", "Chrome", "Application", "chrome.exe"),
    join(process.env.LOCALAPPDATA || "", "Google", "Chrome", "Application", "chrome.exe"),
    join(process.env["PROGRAMFILES(X86)"] || "", "Google", "Chrome", "Application", "chrome.exe"),
  ];
  return candidates.find((path) => path && existsSync(path)) || null;
}

function openWebXrBrowser() {
  const exe = findChromium();
  if (!exe) {
    spawn("cmd", ["/c", "start", "", pageUrl], { detached: true, stdio: "ignore", windowsHide: true }).unref();
    console.log("Edge/Chrome not found. Opened the default browser; WebXR may be missing.");
    return;
  }
  const isEdge = /msedge\.exe$/i.test(exe);
  const profile = join(tmpdir(), isEdge ? "OfficeWebXR-Edge" : "OfficeWebXR-Chrome");
  spawn(exe, [
    `--user-data-dir=${profile}`,
    "--no-first-run",
    "--no-default-browser-check",
    "--disable-sync",
    "--disable-features=XRSandbox",
    "--force-webxr-runtime=openxr",
    pageUrl,
  ], {
    detached: true,
    stdio: "ignore",
    env: process.env,
  }).unref();
  console.log(`Opened WebXR browser: ${exe}`);
  console.log("Use this new Edge/Chrome window. Old tabs will not see the headset.");
}

async function boot() {
  freePort(port);
  if (process.env.OPEN_BROWSER === "1") {
    const steamOk = await ensureSteamVr();
    if (!steamOk) {
      console.log("SteamVR is not running. 进入 VR will stay inside the browser window.");
    }
  }
  await new Promise((resolve, reject) => {
    const onError = (error) => reject(error);
    server.once("error", onError);
    server.listen(port, host, () => {
      server.off("error", onError);
      resolve();
    });
  });
  server.on("error", (error) => {
    console.error("Office WebXR server error", error);
  });
  console.log(`Office WebXR: ${pageUrl}`);
  if (process.env.OPEN_BROWSER === "1") {
    openWebXrBrowser();
  }
}

boot().catch((error) => {
  console.error(error);
  process.exit(1);
});
