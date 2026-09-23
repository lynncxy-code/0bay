# Haiju North exterior environment

- Source: `Source/Haiju_North_Environment.glb` (complete V3 blue asset, SHA-256 `E508EEAFFB71299F43E3B03C4B7156CA1A4E10D5AC46EAEC5C453227F088CFB8`).
- UE assets: `/Game/HaijuNorthBeta/Imported/Haiju_North_Environment`.
- Background level: `/Game/HaijuNorthBeta/Maps/L_HaijuNorthBeta_Exterior`, always loaded by `/Game/OfficeShell/Maps/Office_Demo`.
- Placement: location `(14485.456, -5394.377, 0) cm`, yaw `20.9°`, scale `1`.
- Alignment: best-fit against the existing Office satellite Landscape using building footprints, roads and the stadium. The stadium control residual is about `10.77 px × 3.5228 m/px = 37.9 m`; this is visual registration, not survey-grade georeferencing.
- Background actors use `NoCollision`, generate no overlap events, do not affect navigation, and therefore do not participate in runtime click traces. Their cast shadows and distance-field lighting are disabled to keep the wide-area context lightweight and avoid Virtual Shadow Map page pressure.
- The source GLB is meter-scale, X=east, Y=up and -Z=north. UE Interchange converts it to centimeters and Z-up; no actor scale compensation is required.

## Current campus additions

The stadium and science courtyard now use the local refined replacements described
in [Landmarks/README.md](Landmarks/README.md), with 1,676 current pick targets after
merging the stadium entrance towers into the main stadium target.

See [Campus500/README.md](Campus500/README.md) for the modeled 500 m ground,
individual building hover/click highlights, controls, rebuild steps and validation.
The original `Ground_7` actor now has Z = +3 cm, placing its surface at +1 cm above
the old imagery. This is the sole transform exception to the shared anchor above.
Individual picking is provided by the new campus interaction actor; the original
background mesh actors still have no runtime collision or picking.
