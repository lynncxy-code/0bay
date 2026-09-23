# Campus ground and building interaction — 2026-09-22

Update 2026-09-23: [the landmark overlay](../Landmarks/README.md) replaces the stadium
and science courtyard shapes. There are now 1,676 targets because the stadium and
its two entrance towers share one selection. Original ground/detail geometry is unchanged.

The approximately 500 m area around GrandNeoBay now uses modeled ground, roads,
entrance paving, planting, parking pockets, benches and lights. These additions
are illustrative landscape design, not surveyed facilities. No satellite texture
is used by any new material. The old Landscape remains beneath the modeled ground
and may be visible beyond the modeled area.

## Contents

- `Campus500.glb`: 13 meshes, approximately 1.4 MB; 83 garden islands, 290 trees,
  192 marked parking bays, 63 benches and 63 light posts. No new lights cast shadows.
- `building_shapes.json`: independent outline/triangle data for 1,677 existing
  surrounding buildings plus the two-wing GrandNeoBay building as one target.
- `generation_report.json`: dimensions, center and geometry counts.
- UE level: `/Game/HaijuNorthBeta/Maps/L_Campus500_Details`, always loaded by
  `/Game/OfficeShell/Maps/Office_Demo`. It adds 13 StaticMeshActors and one interaction actor.

## Interaction

In the existing F9 building preview, hovering a building shows the same cyan skin
and rim style as the ninth floor. Clicking retains one building highlight; empty
space or Esc clears it. A second Esc returns to the office. Clicking the ninth
floor keeps its drawer/card/entry behavior. Selecting a building retracts the
floor; selecting the floor clears the whole-building highlight.

The shared preview camera can now zoom out to 1,600 m. Office movement/input is
unchanged. Building selection is currently available in this preview, not in the
office's existing god-view/free-roam mode, whose behavior remains to be specified.

`ACampusInteraction` uses CPU triangle picking with a per-building bounds filter.
Only one shared glass mesh and one shared rim mesh render at a time. The pick
shapes have no physics collision, overlap events, navigation influence or tick.
Geometry is stored in the map, so runtime does not read source JSON or need Blender.
The built-in UE ProceduralMeshComponent plugin renders these two shared meshes.

## Ground elevations

Only `HaijuNorthBeta_152_Ground_7` in the original exterior sublevel changed:
Actor Z = +3 cm, making its surface Z = +1 cm. The original asphalt remains at
Z = +6 cm and sidewalk bounds at +2.5 to +28.5 cm. The original background
building and road transforms/materials are retained.

The original model ends south of the main building. A matching ground extension
now covers that gap to a 540 m radius, with road continuations to 530 m; details
are constrained to approximately 500 m. The extension shares the original
ground edge without coplanar overlapping polygons. Entrance paving is flush,
not a raised building pedestal.

## Rebuild and validation

1. Run Blender with `--background --python Tools/HaijuNorthBeta/build_campus.py`.
2. Build `OfficeHostEditor Win64 Development -Module=OfficeHost`.
3. Run UE Python `Tools/HaijuNorthBeta/raise_ground.py` if the original ground needs correction.
4. Run UE Python `Tools/HaijuNorthBeta/install_campus.py`.
5. Reload and run `Tools/HaijuNorthBeta/verify_campus.py`.
6. Run Standalone DX12 with `-CampusTest` and separately `-GNBTest` for runtime
   picking, hover, selection, floor entry and input-restoration checks.

The installer owns only the new detail level and takes a timestamped map backup.
Source data is copied into `../Source/cropped_scene.json`; existing OSM/GBA source
license and accuracy limitations continue to apply. Main-building proxy generation
uses the original CAD outline in `D:/0bay/output/GrandNeoBay_Game/scripts/build_building.py`.

Acceptance artifacts are in `../Acceptance/`. This is an Editor/Standalone DX12
acceptance pass, not a packaged Development/Shipping or backend binding acceptance.
The existing OntoTwin dataset binding HTTP 409 log messages and occasional global
VSM warnings belong to the existing host session and were not changed here.
