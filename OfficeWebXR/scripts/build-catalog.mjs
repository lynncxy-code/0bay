import { readFile, writeFile, mkdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { builtinItems, itemFromPdfRecord } from "../src/catalog.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const pdfPath = resolve(root, "../Content/Art/家具_PDF匹配/完整清单.json");
const outDir = resolve(root, "data");

const raw = JSON.parse(await readFile(pdfPath, "utf8"));
const fromPdf = raw.map(itemFromPdfRecord);
const builtins = builtinItems();
const byId = new Map();
for (const item of fromPdf) {
  byId.set(item.catalogId, item);
}
for (const item of builtins) {
  byId.set(item.catalogId, item);
}
const catalog = [...byId.values()].sort((a, b) => a.catalogId - b.catalogId);
await mkdir(outDir, { recursive: true });
await writeFile(resolve(outDir, "inspect-catalog.json"), `${JSON.stringify(catalog, null, 2)}\n`, "utf8");
console.log(`Wrote ${catalog.length} catalog items to data/inspect-catalog.json`);
