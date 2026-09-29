const ARCHITECTURE_TOKENS = [
  "SM_Wall",
  "SM_Floor",
  "SM_Ceiling",
  "SM_Column",
  "SM_Facade",
  "SM_Roof",
  "SM_Cove",
  "SM_Skirting",
  "SM_Core",
  "SM_Door",
  "SM_Interior",
  "SM_Exterior",
  "SM_Partition",
  "SM_Feature",
  "Sky",
  "Landscape",
  "BP_OfficeShell",
  "WorldDataLayers",
  "Brush",
  "Light",
  "Camera",
  "PlayerStart",
  "SkyAtmosphere",
];

export function isArchitectureName(name) {
  const haystack = String(name || "");
  return ARCHITECTURE_TOKENS.some((token) => haystack.includes(token));
}

export function inferCategory(displayName) {
  const name = String(displayName || "");
  if (name.includes("电视") || name.includes("TV") || name.includes("屏") || name.includes("冰箱") || name.includes("打印")) {
    return "电器";
  }
  if (name.includes("椅") || name.includes("沙发") || name.includes("凳")) {
    return "座椅";
  }
  if (name.includes("桌") || name.includes("几") || name.includes("吧台")) {
    return "桌台";
  }
  if (name.includes("柜") || name.includes("文件")) {
    return "储物";
  }
  if (name.includes("电话亭")) {
    return "隔间";
  }
  if (name.includes("植物") || name.includes("绿萝")) {
    return "绿化";
  }
  return "家具";
}

export function formatDimensionsMm(mm) {
  if (!Array.isArray(mm) || mm.length < 3) {
    return "待现场复核";
  }
  return `${Math.round(mm[0])} × ${Math.round(mm[1])} × ${Math.round(mm[2])} mm`;
}

export function toPanelText(item) {
  return [
    item.displayName,
    "",
    `类别    ${item.category}`,
    `区域    ${item.room}`,
    `尺寸    ${item.dimensions}`,
    `材质    ${item.material}`,
    `资产编号  ${item.assetId}`,
    `所属系统  ${item.systemName}`,
    `维保周期  ${item.maintenanceCycle}`,
    `下次维保  ${item.nextMaintenance}`,
    "",
    item.description,
  ].join("\n");
}

export function findForName(actorOrMeshName, items) {
  if (!actorOrMeshName) {
    return null;
  }
  const haystack = String(actorOrMeshName);
  const haystackLower = haystack.toLowerCase();
  let bestScore = 0;
  let best = null;
  for (const item of items) {
    let score = 0;
    if (item.displayName && haystack.includes(item.displayName)) {
      score = Math.max(score, item.displayName.length + 10);
    }
    for (const key of item.matchKeys || []) {
      if (!key) {
        continue;
      }
      if (haystack.includes(key) || haystackLower.includes(String(key).toLowerCase())) {
        score = Math.max(score, key.length);
      }
    }
    if (score > bestScore) {
      bestScore = score;
      best = item;
    }
  }
  if (!best || bestScore < 2) {
    return null;
  }
  return best;
}

export function walkNodes(root, visit) {
  const stack = [root];
  while (stack.length) {
    const node = stack.pop();
    if (!node) {
      continue;
    }
    visit(node);
    const children = node.children || [];
    for (let i = children.length - 1; i >= 0; i -= 1) {
      stack.push(children[i]);
    }
  }
}

export function resolveInspectableRoot(node) {
  let current = node;
  let found = null;
  while (current) {
    if (current.userData?.inspectable && current.userData?.item) {
      found = current;
    }
    current = current.parent;
  }
  return found;
}

export function bindUnmatchedFurniture(root, items) {
  let bound = 0;
  walkNodes(root, (node) => {
    const name = node.name || "";
    if (!name || isArchitectureName(name) || node.userData?.inspectable) {
      return;
    }
    if (resolveInspectableRoot(node.parent)) {
      return;
    }
    const item = findForName(name, items) || findForName(`${name} ${node.parent?.name || ""}`, items);
    if (!item) {
      return;
    }
    node.userData.inspectable = true;
    node.userData.item = item;
    bound += 1;
  });
  return bound;
}

export function unifyInspectableParts(root) {
  const nodes = [];
  walkNodes(root, (node) => nodes.push(node));
  for (const node of nodes) {
    if (!node.userData?.inspectable) {
      continue;
    }
    const ancestor = resolveInspectableRoot(node.parent);
    if (ancestor && ancestor !== node) {
      node.userData.inspectable = false;
    }
  }
  for (let i = nodes.length - 1; i >= 0; i -= 1) {
    const parent = nodes[i];
    const children = parent.children || [];
    const parts = children.filter((child) => child.userData?.inspectable && child.userData?.item);
    if (parts.length < 2 || !parent.parent || isArchitectureName(parent.name)) {
      continue;
    }
    const catalogId = parts[0].userData.item.catalogId;
    if (parts.some((part) => part.userData.item.catalogId !== catalogId)) {
      continue;
    }
    const parentLooksLikeFurniture = findForName(parent.name || "", [parts[0].userData.item])
      || /PDF_|\d+_/i.test(parent.name || "")
      || children.length === parts.length;
    if (!parentLooksLikeFurniture) {
      continue;
    }
    parent.userData.inspectable = true;
    parent.userData.item = parts[0].userData.item;
    for (const part of parts) {
      part.userData.inspectable = false;
    }
  }
}

export function applyBindings(root, bindingFile, items) {
  const byId = new Map(items.map((item) => [item.catalogId, item]));
  const bindings = bindingFile?.bindings || [];
  let bound = 0;
  walkNodes(root, (node) => {
    const name = node.name || "";
    if (isArchitectureName(name)) {
      return;
    }
    const hit = bindings.find((binding) => name === binding.nodeName || name.includes(binding.nodeName));
    if (!hit) {
      return;
    }
    const item = byId.get(hit.catalogId);
    if (!item) {
      return;
    }
    node.userData.inspectable = true;
    node.userData.item = item;
    bound += 1;
  });
  return bound;
}

export function catalogIdToAssetId(id) {
  return `OFC-${String(id).padStart(3, "0")}`;
}

export function itemFromPdfRecord(record) {
  const id = Number(record.id);
  const name = record.name || "";
  const keys = [];
  if (record.file) {
    keys.push(String(record.file).replace(/\.[^.]+$/, ""));
  }
  keys.push(name);
  keys.push(`PDF_${String(id).padStart(2, "0")}`);
  return {
    catalogId: id,
    displayName: name,
    category: inferCategory(name),
    room: record.room || "办公室",
    dimensions: formatDimensionsMm(record.dimensions_mm),
    material: record.material_note || "面层按场景近似，未对色卡",
    assetId: catalogIdToAssetId(id),
    systemName: "办公家具",
    maintenanceCycle: "12个月（示例）",
    nextMaintenance: "2027-03-15（示例）",
    description: record.geometry_note || "",
    matchKeys: keys,
  };
}

export function builtinItems() {
  return [
    {
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
      matchKeys: [
        "newline_lyrapro_interactive_display",
        "SM_TV_Plain",
        "TV_Plain_NoStand",
        "电视",
        "tv-stand",
        "PDF_101",
      ],
    },
    {
      catalogId: 104,
      displayName: "冰箱",
      category: "电器",
      room: "打印茶水区",
      dimensions: "待现场复核",
      material: "金属壳体",
      assetId: "OFC-104",
      systemName: "办公电器",
      maintenanceCycle: "12个月（示例）",
      nextMaintenance: "2027-03-15（示例）",
      description: "茶水区冰箱。一期仅支持查看属性。",
      matchKeys: ["Fridge_Top_Housing", "Fridge", "冰箱"],
    },
    {
      catalogId: 105,
      displayName: "打印机",
      category: "电器",
      room: "打印茶水区",
      dimensions: "待现场复核",
      material: "塑料壳体",
      assetId: "OFC-105",
      systemName: "办公电器",
      maintenanceCycle: "12个月（示例）",
      nextMaintenance: "2027-03-15（示例）",
      description: "打印设备。一期仅支持查看属性。",
      matchKeys: ["Upper_Printer_Door", "Printer", "打印机"],
    },
    {
      catalogId: 106,
      displayName: "文化墙",
      category: "展示",
      room: "公共区域",
      dimensions: "墙面展板",
      material: "板材 + 画面",
      assetId: "OFC-106",
      systemName: "空间展示",
      maintenanceCycle: "12个月（示例）",
      nextMaintenance: "2027-03-15（示例）",
      description: "企业文化展示墙。一期仅支持查看属性。",
      matchKeys: ["SM_CultureWall", "CultureWall", "文化墙"],
    },
    {
      catalogId: 102,
      displayName: "双人电话亭",
      category: "隔间",
      room: "打印茶水区",
      dimensions: "约 1600 mm 宽封闭舱",
      material: "玻璃 + 织物 + 金属型材",
      assetId: "OFC-102",
      systemName: "办公家具",
      maintenanceCycle: "12个月（示例）",
      nextMaintenance: "2027-03-15（示例）",
      description: "隔音通话舱。一期仅支持查看属性，不可开关门。",
      matchKeys: ["PhoneBooth", "电话亭", "PB26", "PDF_102"],
    },
    {
      catalogId: 103,
      displayName: "绿萝盆栽",
      category: "绿化",
      room: "公共区域",
      dimensions: "盆栽尺度",
      material: "植物 + 陶盆",
      assetId: "OFC-103",
      systemName: "办公家具",
      maintenanceCycle: "12个月（示例）",
      nextMaintenance: "2027-03-15（示例）",
      description: "场景绿化点缀。",
      matchKeys: ["Pothos", "绿萝", "PDF_103"],
    },
    {
      catalogId: 0,
      displayName: "办公室 VR 操作说明",
      category: "系统",
      room: "公司大门 / 出生点",
      dimensions: "-",
      material: "-",
      assetId: "OFC-HELP",
      systemName: "VR 交互",
      maintenanceCycle: "-",
      nextMaintenance: "-",
      description: "左摇杆或 WASD 行走。右摇杆左右或 Q/E 转身。右摇杆下推对准地面后松开瞬移。右手扳机或鼠标左键查看属性。B / 鼠标右键关闭面板。",
      matchKeys: ["HelpPlaque", "操作说明"],
    },
  ];
}

export function mergeCatalogItems(base, extra) {
  const byId = new Map();
  for (const item of extra || []) {
    byId.set(item.catalogId, { ...item, matchKeys: [...(item.matchKeys || [])] });
  }
  for (const item of base || []) {
    const prev = byId.get(item.catalogId);
    if (!prev) {
      byId.set(item.catalogId, { ...item, matchKeys: [...(item.matchKeys || [])] });
      continue;
    }
    const keys = new Set([...(prev.matchKeys || []), ...(item.matchKeys || [])]);
    byId.set(item.catalogId, { ...item, matchKeys: [...keys] });
  }
  return [...byId.values()].sort((left, right) => Number(left.catalogId) - Number(right.catalogId));
}

export async function loadJson(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Failed to load ${url}: ${response.status}`);
  }
  return response.json();
}

export async function loadCatalogAndBindings(dataRoot = "/data") {
  let items = builtinItems();
  try {
    const catalog = await loadJson(`${dataRoot}/inspect-catalog.json`);
    if (Array.isArray(catalog) && catalog.length) {
      items = mergeCatalogItems(catalog, builtinItems());
    }
  } catch {
    // Greybox uses builtins until the UE exporter writes inspect-catalog.json.
  }
  let bindings = { bindings: [] };
  try {
    bindings = await loadJson(`${dataRoot}/inspect-bindings.json`);
  } catch {
    bindings = { bindings: [] };
  }
  return { items, bindings };
}
