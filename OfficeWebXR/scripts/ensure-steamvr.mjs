import { existsSync } from "node:fs";
import { execSync, spawn } from "node:child_process";
import { join } from "node:path";

export function firstExisting(paths) {
  return paths.find((path) => path && existsSync(path)) || null;
}

export function findSteamExe() {
  return firstExisting([
    "D:\\Steam\\steam.exe",
    join(process.env.PROGRAMFILES || "", "Steam", "steam.exe"),
    join(process.env["PROGRAMFILES(X86)"] || "", "Steam", "steam.exe"),
    "C:\\Program Files (x86)\\Steam\\steam.exe",
  ]);
}

export function findSteamVrRuntime() {
  return firstExisting([
    "D:\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json",
    join(process.env["PROGRAMFILES(X86)"] || "", "Steam", "steamapps", "common", "SteamVR", "steamxr_win64.json"),
    join(process.env.PROGRAMFILES || "", "Steam", "steamapps", "common", "SteamVR", "steamxr_win64.json"),
    "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json",
  ]);
}

export function findSteamVrMonitor() {
  return firstExisting([
    "D:\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\vrmonitor.exe",
    join(process.env["PROGRAMFILES(X86)"] || "", "Steam", "steamapps", "common", "SteamVR", "bin", "win64", "vrmonitor.exe"),
    join(process.env.PROGRAMFILES || "", "Steam", "steamapps", "common", "SteamVR", "bin", "win64", "vrmonitor.exe"),
    "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\vrmonitor.exe",
  ]);
}

export function findSteamVrStartup() {
  return firstExisting([
    "D:\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\vrstartup.exe",
    join(process.env["PROGRAMFILES(X86)"] || "", "Steam", "steamapps", "common", "SteamVR", "bin", "win64", "vrstartup.exe"),
    join(process.env.PROGRAMFILES || "", "Steam", "steamapps", "common", "SteamVR", "bin", "win64", "vrstartup.exe"),
    "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\vrstartup.exe",
  ]);
}

export function parseListeningPids(netstatOutput, port) {
  const pids = new Set();
  const needle = `:${port}`;
  for (const line of (netstatOutput || "").split(/\r?\n/)) {
    if (!line.includes(needle) || !/LISTENING/i.test(line)) {
      continue;
    }
    const pid = line.trim().split(/\s+/).pop();
    if (pid && /^\d+$/.test(pid)) {
      pids.add(pid);
    }
  }
  return [...pids];
}

export function steamVrRunningFromTasklist(tasklistOutput) {
  return /vrmonitor\.exe/i.test(tasklistOutput || "") || /vrserver\.exe/i.test(tasklistOutput || "");
}

function run(command) {
  try {
    return execSync(command, { encoding: "utf8", windowsHide: true, stdio: ["ignore", "pipe", "ignore"] });
  } catch (error) {
    return error.stdout || "";
  }
}

export function freePort(port, currentPid = process.pid) {
  const pids = parseListeningPids(run("netstat -ano"), port).filter((pid) => pid !== String(currentPid));
  for (const pid of pids) {
    console.log(`Stopping leftover server PID ${pid} on port ${port}`);
    run(`taskkill /F /PID ${pid}`);
  }
  return pids;
}

export function isSteamVrRunning() {
  return steamVrRunningFromTasklist(run("tasklist"));
}

export function launchSteamVr() {
  const steam = findSteamExe();
  const monitor = findSteamVrMonitor();
  const startup = findSteamVrStartup();
  if (steam) {
    spawn(steam, ["-applaunch", "250820"], { detached: true, stdio: "ignore", windowsHide: false }).unref();
    console.log(`Launching SteamVR via Steam: ${steam}`);
  }
  if (monitor) {
    spawn(monitor, [], { detached: true, stdio: "ignore", windowsHide: false }).unref();
    console.log(`Launching SteamVR monitor: ${monitor}`);
  } else if (startup) {
    spawn(startup, [], { detached: true, stdio: "ignore", windowsHide: false }).unref();
    console.log(`Launching SteamVR startup: ${startup}`);
  } else {
    console.log("SteamVR executable not found. Edge WebXR will stay in the browser window.");
    return false;
  }
  return true;
}

export async function waitForSteamVr(timeoutMs = 45000, intervalMs = 1000) {
  const started = Date.now();
  while (Date.now() - started < timeoutMs) {
    if (isSteamVrRunning()) {
      console.log("SteamVR is running. Keep PICO Connect connected.");
      return true;
    }
    await new Promise((resolve) => setTimeout(resolve, intervalMs));
  }
  console.log("SteamVR did not appear. Clicking 进入 VR will stay in the browser.");
  return false;
}

export async function ensureSteamVr() {
  const runtime = findSteamVrRuntime();
  if (runtime) {
    process.env.XR_RUNTIME_JSON = runtime;
    try {
      execSync(`reg add "HKCU\\SOFTWARE\\Khronos\\OpenXR\\1" /v ActiveRuntime /t REG_SZ /d "${runtime}" /f`, {
        stdio: "ignore",
        windowsHide: true,
      });
    } catch {
      // ignore registry failures; start.cmd also writes this
    }
    console.log(`OpenXR runtime: ${runtime}`);
  } else {
    console.log("SteamVR OpenXR json not found.");
  }
  if (!isSteamVrRunning()) {
    launchSteamVr();
  } else {
    console.log("SteamVR already running.");
  }
  return waitForSteamVr();
}

if (process.argv[1] && process.argv[1].replaceAll("\\", "/").endsWith("ensure-steamvr.mjs")) {
  ensureSteamVr().then((ok) => {
    process.exit(ok ? 0 : 1);
  });
}
