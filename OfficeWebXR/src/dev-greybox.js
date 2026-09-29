import * as THREE from "three";
import { builtinItems } from "./catalog.js";

export const GREYBOX_SPAWN = {
  gltfUp: "y",
  units: "cm",
  playerStart: { x: 0, y: 0, z: 92 },
  capsuleRadius: 34,
  capsuleHalfHeight: 92,
  eyeHeightFromFloor: 172,
  walkSpeed: 220,
  snapTurnDegrees: 45,
  laserMaxDistance: 1200,
};

export function createGreyboxScene() {
  const root = new THREE.Group();
  root.name = "GreyboxOffice";

  const floor = new THREE.Mesh(
    new THREE.PlaneGeometry(800, 800),
    new THREE.MeshBasicMaterial({ color: 0x6d7b8c, side: THREE.DoubleSide }),
  );
  floor.rotation.x = -Math.PI / 2;
  floor.userData.collision = true;
  floor.userData.floor = true;
  floor.name = "SM_Floor_Greybox";
  root.add(floor);

  const grid = new THREE.GridHelper(800, 16, 0x4a6280, 0x334155);
  grid.position.y = 0.5;
  root.add(grid);

  const wallMaterial = new THREE.MeshBasicMaterial({ color: 0x8b97a8, side: THREE.DoubleSide });
  const walls = [
    { name: "SM_Wall_North", pos: [0, 150, -400], size: [800, 300, 16] },
    { name: "SM_Wall_South", pos: [0, 150, 400], size: [800, 300, 16] },
    { name: "SM_Wall_West", pos: [-400, 150, 0], size: [16, 300, 800] },
    { name: "SM_Wall_East", pos: [400, 150, 0], size: [16, 300, 800] },
  ];
  for (const wall of walls) {
    const mesh = new THREE.Mesh(new THREE.BoxGeometry(...wall.size), wallMaterial);
    mesh.position.set(...wall.pos);
    mesh.name = wall.name;
    mesh.userData.collision = true;
    mesh.userData.wall = true;
    root.add(mesh);
  }

  const items = builtinItems();
  const boxes = [
    { catalogId: 101, color: 0x5aa0ff, position: [-120, 40, -320], size: [80, 80, 12] },
    { catalogId: 102, color: 0x6fd08a, position: [0, 80, -360], size: [70, 160, 70] },
    { catalogId: 103, color: 0x7ee05a, position: [120, 30, -320], size: [40, 60, 40] },
    { catalogId: 0, color: 0xffd56a, position: [0, 50, -140], size: [60, 100, 8] },
  ];
  for (const box of boxes) {
    const item = items.find((entry) => entry.catalogId === box.catalogId);
    const mesh = new THREE.Mesh(
      new THREE.BoxGeometry(...box.size),
      new THREE.MeshBasicMaterial({ color: box.color }),
    );
    mesh.position.set(...box.position);
    mesh.name = item.matchKeys[0];
    mesh.userData.inspectable = true;
    mesh.userData.item = item;
    root.add(mesh);
  }

  return root;
}
