import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import { createServer, request as httpRequest } from "node:http";
import { extname, join, normalize, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const host = process.env.HOST || "0.0.0.0";
const port = Number(process.env.PORT || 4173);
const twinBackend = new URL(process.env.TWIN_BACKEND || "http://localhost:5000");
const root = resolve(fileURLToPath(new URL(".", import.meta.url)));
const projectRoot = resolve(fileURLToPath(new URL("..", import.meta.url)));
const dataRoot = resolve(process.env.DATA_ROOT || join(projectRoot, "data"));
const contentTypes = {
  ".css": "text/css; charset=utf-8",
  ".bin": "application/octet-stream",
  ".cubemips": "application/octet-stream",
  ".glb": "model/gltf-binary",
  ".gltf": "model/gltf+json",
  ".hdr": "image/vnd.radiance",
  ".html": "text/html; charset=utf-8",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".mp4": "video/mp4",
  ".wav": "audio/wav",
};

function proxyTwinRequest(request, response, requestPath) {
  const upstreamPath = requestPath.replace(/^\/twin-api/, "") || "/";
  const headers = {
    accept: request.headers.accept || "application/json",
    "content-type": request.headers["content-type"] || "application/json",
    "x-ontotwin-ue-project-id": request.headers["x-ontotwin-ue-project-id"] || "ueproj_ZHHZ_NEW",
    "x-ontotwin-ue-project-name": request.headers["x-ontotwin-ue-project-name"] || "ZHHZ_NEW",
  };
  const upstream = httpRequest({
    hostname: twinBackend.hostname,
    port: twinBackend.port || 80,
    method: request.method,
    path: upstreamPath + (new URL(request.url, `http://${host}:${port}`).search || ""),
    headers,
  }, (upstreamResponse) => {
    response.writeHead(upstreamResponse.statusCode || 502, {
      "Cache-Control": "no-store",
      "Content-Type": upstreamResponse.headers["content-type"] || "application/json; charset=utf-8",
      "X-ZHHZ-Twin-Proxy": "1",
    });
    upstreamResponse.pipe(response);
  });
  upstream.setTimeout(15000, () => upstream.destroy(new Error("Twin backend timeout")));
  upstream.on("error", (error) => {
    if (!response.headersSent) response.writeHead(502, { "Content-Type": "application/json; charset=utf-8" });
    response.end(JSON.stringify({ error: "twin_backend_unavailable", detail: error.message }));
  });
  request.pipe(upstream);
}

const server = createServer(async (request, response) => {
  let requestPath;
  try {
    requestPath = decodeURIComponent(new URL(request.url, `http://${host}:${port}`).pathname);
  } catch {
    response.writeHead(400, { "Content-Type": "text/plain; charset=utf-8" });
    response.end("Invalid URL encoding");
    return;
  }
  if (requestPath === "/twin-api" || requestPath.startsWith("/twin-api/")) {
    proxyTwinRequest(request, response, requestPath);
    return;
  }
  const servesData = requestPath === "/data" || requestPath.startsWith("/data/");
  const sourceRoot = servesData ? dataRoot : root;
  const relativePath = servesData
    ? requestPath.replace(/^\/data\/?/, "")
    : requestPath === "/" ? "index.html" : requestPath.slice(1);
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
    const cacheControl = [".bin", ".cubemips", ".glb", ".gltf", ".hdr", ".jpg", ".jpeg", ".png", ".mp4"]
      .includes(extension)
      ? "public, max-age=31536000, immutable"
      : "no-store";
    const rangeMatch = /^bytes=(\d*)-(\d*)$/.exec(request.headers.range || "");
    if (rangeMatch) {
      const requestedStart = rangeMatch[1] === "" ? null : Number(rangeMatch[1]);
      const requestedEnd = rangeMatch[2] === "" ? null : Number(rangeMatch[2]);
      const suffixLength = requestedStart === null ? requestedEnd : null;
      const start = suffixLength !== null
        ? Math.max(0, fileStat.size - suffixLength)
        : Math.min(requestedStart ?? 0, fileStat.size - 1);
      const end = Math.min(requestedEnd ?? (fileStat.size - 1), fileStat.size - 1);
      if (!Number.isFinite(start) || !Number.isFinite(end) || start > end || fileStat.size === 0) {
        response.writeHead(416, { "Content-Range": `bytes */${fileStat.size}` });
        response.end();
        return;
      }
      response.writeHead(206, {
        "Accept-Ranges": "bytes",
        "Cache-Control": cacheControl,
        "Content-Length": end - start + 1,
        "Content-Range": `bytes ${start}-${end}/${fileStat.size}`,
        "Content-Type": contentType,
      });
      if (request.method === "HEAD") response.end();
      else createReadStream(filePath, { start, end }).pipe(response);
      return;
    }
    response.writeHead(200, {
      "Accept-Ranges": "bytes",
      "Cache-Control": cacheControl,
      "Content-Length": fileStat.size,
      "Content-Type": contentType,
    });
    if (request.method === "HEAD") response.end();
    else createReadStream(filePath).pipe(response);
  } catch {
    response.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    response.end("Not found");
  }
});

server.listen(port, host, () => {
  console.log(`X300 web runtime: http://${host}:${port}`);
});
