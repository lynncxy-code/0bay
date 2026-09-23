# Refined stadium and science courtyard — 2026-09-23

This is a local replacement of the Huo Ying Dong stadium and SJTU science
courtyard. The surrounding campus, ground, roads and other buildings remain intact.
The frozen source delivery is copied from
`D:/tmp/digital_twin_aircraft/beta/haiju_window_scene/output_landmarks`.

## Asset and mapping contract

- `Landmarks_Combined.glb`: 48 meshes, 26,844 source triangles.
- `Background_Changed_Batches.glb`: nine remainder meshes, 7,854 source triangles.
- `replacement_manifest.json`: explicit mapping of 12 original batches; nine
  receive filtered remainder meshes and three empty original batches are hidden.
- `landmark_pick_shapes.json`: eight roof-aware picking/highlight shapes. The
  two stadium entrance towers share the stadium's original key. Seven science
  blocks retain their original keys. Total campus targets: 1,676.
- `validation.json`: source delivery hashes and independent geometry/UV comparison.

All coordinates retain the original source-global metre system. UE imports in
centimetres, using the existing anchor `(14485.456288654983,-5394.3765929212095,0)`,
yaw `20.9`, scale `1`. No additional rotation or scale is baked into the assets.
The stadium mesh maximum is 29.341 m; its pick skin extends 0.12 m above the roof.
Science tower/wing heights are reference-based estimates, not surveyed dimensions.

## UE installation

`/Game/HaijuNorthBeta/Maps/L_RefinedLandmarks` contains only the 48 new mesh actors
and is Always Loaded. The original exterior level retains all 158 actor identities
and transforms. Nine target actors point to new remainder assets but keep their
exact original UE material references; three empty batches are invisible in game
and editor via actor hidden/component visibility flags. Original assets are kept.

`L_Campus500_Details` changes only its interaction actor's target geometry: replace
10 old keys with eight new shapes, preserving the other **1,668** shapes exactly.
Its 13 landscape meshes, placements, materials and interaction material references
remain unchanged. The ground +3 cm actor offset / +1 cm surface is retained.

Run UE Python `Tools/HaijuNorthBeta/install_landmarks.py` to reapply the frozen
delta. It backs up the maps before writing. Do not run a full campus reinstall
just to replace these landmarks. `verify_landmarks.py` compares saved/reloaded
state against the pre-install snapshot, including original actor transforms,
material references, all 146 unrelated background actors and non-target shape hashes.

Rebuild protection is centralized in `landmark_contract.py`: `build_campus.py`
and `install_campus.py` apply the same key overlay; a full exterior rebuild calls
the landmark installer afterward. The legacy Saved exterior installer forwards
to the tracked Tools script. This prevents a later rebuild from reviving the old
flat landmark boxes or their obsolete picking shapes.

## Acceptance and recovery

See `../Acceptance/Landmarks/` for final UE screenshots and runtime/state reports.
`-LandmarkTest` checks the new roof/tower rays, precise hit heights, actual preview
camera ownership, projected UI clicks, source mesh triangle counts and clean return.
`-CampusTest` and `-GNBTest` cover campus hover/selection and the ninth-floor flow.
Tests allow the existing host's asynchronous dataset/camera initialization to finish
before opening the preview. This is not a change to the host's startup behavior.

Nanite fallback triangle counts are recorded separately from MeshDescription source
counts. The smaller fallback mesh does not mean that source geometry was deleted.

Initial backup: `Saved/HaijuNorthBeta/LandmarkBackup_20260923_105226/`.
To roll back the scene, close OfficeShell and restore `Office_Demo.umap`,
`L_HaijuNorthBeta_Exterior.umap` and `L_Campus500_Details.umap` from that backup;
the unused new assets may remain. A subsequent rebuild will intentionally reapply
the landmark delta while its manifest is present, so disable that overlay before
rebuilding a rolled-back version. The GrandNeoBay map backup is for evidence only;
this task did not edit it.

No Git commit/push, Cook, packaged build or plugin/business-data change is included.
