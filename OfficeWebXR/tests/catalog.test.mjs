import test from "node:test";
import assert from "node:assert/strict";
import {
  toPanelText,
  inferCategory,
  formatDimensionsMm,
  findForName,
  applyBindings,
  bindUnmatchedFurniture,
  unifyInspectableParts,
  resolveInspectableRoot,
  mergeCatalogItems,
  isArchitectureName,
} from "../src/catalog.js";

const tv = {
  catalogId: 101,
  displayName: "会议室电视",
  category: "电器",
  room: "接待区 / 会议室",
  dimensions: "约 122 × 14 × 81 cm",
  material: "塑料壳体 + 玻璃屏（CC0 场景资产）",
  assetId: "OFC-101",
  systemName: "办公家具",
  maintenanceCycle: "12个月（示例）",
  nextMaintenance: "2027-03-15（示例）",
  description: "普通无支架电视，用于会议投屏示意。一期仅支持查看属性。",
  matchKeys: ["SM_TV_Plain", "TV_Plain_NoStand", "电视"],
};

test("toPanelText matches FInspectableItemData::ToPanelText", () => {
  const expected = [
    "会议室电视",
    "",
    "类别    电器",
    "区域    接待区 / 会议室",
    "尺寸    约 122 × 14 × 81 cm",
    "材质    塑料壳体 + 玻璃屏（CC0 场景资产）",
    "资产编号  OFC-101",
    "所属系统  办公家具",
    "维保周期  12个月（示例）",
    "下次维保  2027-03-15（示例）",
    "",
    "普通无支架电视，用于会议投屏示意。一期仅支持查看属性。",
  ].join("\n");
  assert.equal(toPanelText(tv), expected);
});

test("inferCategory maps furniture names", () => {
  assert.equal(inferCategory("会议室电视"), "电器");
  assert.equal(inferCategory("打印机"), "电器");
  assert.equal(inferCategory("班椅"), "座椅");
  assert.equal(inferCategory("经理桌"), "桌台");
  assert.equal(inferCategory("文件柜"), "储物");
  assert.equal(inferCategory("双人电话亭"), "隔间");
  assert.equal(inferCategory("绿萝盆栽"), "绿化");
  assert.equal(inferCategory("其它"), "家具");
});

test("formatDimensionsMm matches UE", () => {
  assert.equal(formatDimensionsMm([1800, 1800, 750]), "1800 × 1800 × 750 mm");
  assert.equal(formatDimensionsMm([]), "待现场复核");
});

test("findForName scores mesh keys over weak matches", () => {
  const found = findForName("StaticMeshActor_SM_TV_Plain_01", [tv]);
  assert.equal(found.catalogId, 101);
  assert.equal(findForName("SM_Wall_01", [tv]), null);
});

test("Newline Lyra displays bind as the meeting-room TV", () => {
  const lyra = {
    ...tv,
    matchKeys: ["newline_lyrapro_interactive_display", "SM_TV_Plain", "电视"],
  };
  assert.equal(findForName("newline_lyrapro_interactive_display", [lyra]).catalogId, 101);
  assert.equal(findForName("newline_lyrapro_interactive_display12", [lyra]).catalogId, 101);
  const display = { name: "newline_lyrapro_interactive_display4", userData: {}, children: [], parent: null };
  const cubby = { name: "Display_Cubby_Back_00", userData: {}, children: [], parent: null };
  const root = { name: "Root", userData: {}, children: [display, cubby] };
  display.parent = root;
  cubby.parent = root;
  bindUnmatchedFurniture(root, [lyra]);
  assert.equal(display.userData.inspectable, true);
  assert.equal(display.userData.item.catalogId, 101);
  assert.equal(cubby.userData.inspectable, undefined);
});

test("mergeCatalogItems keeps PDF entries and adds builtin appliance keys", () => {
  const merged = mergeCatalogItems(
    [{ catalogId: 101, displayName: "会议室电视", matchKeys: ["SM_TV_Plain"] }],
    [{ catalogId: 101, displayName: "会议室电视", matchKeys: ["newline_lyrapro_interactive_display"] }, { catalogId: 104, displayName: "冰箱", matchKeys: ["Fridge"] }],
  );
  const tvItem = merged.find((item) => item.catalogId === 101);
  const fridge = merged.find((item) => item.catalogId === 104);
  assert.ok(tvItem.matchKeys.includes("SM_TV_Plain"));
  assert.ok(tvItem.matchKeys.includes("newline_lyrapro_interactive_display"));
  assert.equal(fridge.displayName, "冰箱");
});

test("architecture names are not inspectable", () => {
  assert.equal(isArchitectureName("SM_Floor_Main"), true);
  assert.equal(isArchitectureName("SM_TV_Plain"), false);
});

test("applyBindings marks matching nodes inspectable", () => {
  const root = {
    name: "Root",
    userData: {},
    children: [
      { name: "SM_TV_Plain", userData: {}, children: [] },
      { name: "SM_Floor", userData: {}, children: [] },
    ],
  };
  const bound = applyBindings(root, { bindings: [{ nodeName: "SM_TV_Plain", catalogId: 101 }] }, [tv]);
  assert.equal(bound, 1);
  assert.equal(root.children[0].userData.inspectable, true);
  assert.equal(root.children[0].userData.item.catalogId, 101);
  assert.equal(root.children[1].userData.inspectable, undefined);
});

test("chair seat and back collapse to one inspectable furniture root", () => {
  const item = { catalogId: 13, displayName: "会议椅", matchKeys: ["会议椅", "PDF_13"] };
  const seat = { name: "座垫", userData: { inspectable: true, item }, children: [], isMesh: true };
  const back = { name: "椅背", userData: { inspectable: true, item }, children: [], isMesh: true };
  const chair = {
    name: "PDF_13_会议椅_小会议室_黄色",
    userData: {},
    parent: { name: "Office", userData: {}, children: [] },
    children: [seat, back],
  };
  seat.parent = chair;
  back.parent = chair;
  chair.parent.children = [chair];
  unifyInspectableParts(chair.parent);
  assert.equal(chair.userData.inspectable, true);
  assert.equal(chair.userData.item.catalogId, 13);
  assert.equal(seat.userData.inspectable, false);
  assert.equal(back.userData.inspectable, false);
  assert.equal(resolveInspectableRoot(seat), chair);
  assert.equal(resolveInspectableRoot(back), chair);
});

test("bindUnmatchedFurniture inspects catalog furniture not listed in bindings", () => {
  const desk = { name: "PDF_07_会议桌_大会议室", userData: {}, children: [], parent: null };
  const wall = { name: "SM_Wall_01", userData: {}, children: [], parent: null };
  const root = { name: "Root", userData: {}, children: [desk, wall] };
  desk.parent = root;
  wall.parent = root;
  const deskItem = {
    catalogId: 7,
    displayName: "会议桌",
    matchKeys: ["PDF_07_会议桌_大会议室", "会议桌"],
  };
  bindUnmatchedFurniture(root, [deskItem]);
  assert.equal(desk.userData.inspectable, true);
  assert.equal(desk.userData.item.catalogId, 7);
  assert.equal(wall.userData.inspectable, undefined);
});
