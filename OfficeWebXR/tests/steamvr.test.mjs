import test from "node:test";
import assert from "node:assert/strict";
import {
  parseListeningPids,
  steamVrRunningFromTasklist,
} from "../scripts/ensure-steamvr.mjs";

test("parseListeningPids finds the listener on 4193", () => {
  const output = [
    "  TCP    127.0.0.1:4193         0.0.0.0:0              LISTENING       30080",
    "  TCP    127.0.0.1:3000         0.0.0.0:0              LISTENING       1",
  ].join("\n");
  assert.deepEqual(parseListeningPids(output, 4193), ["30080"]);
});

test("steamVrRunningFromTasklist detects vrmonitor", () => {
  assert.equal(steamVrRunningFromTasklist("vrmonitor.exe  1234 Console"), true);
  assert.equal(steamVrRunningFromTasklist("node.exe  1 Console"), false);
});
