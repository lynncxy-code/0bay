# Business camera framing

Security overview tour:
- Explicitly opening the unfiltered Security business arms one tour after the
  authored overview blend completes. Back from details and layout refreshes do
  not arm another run. Missing bounds/binding leave the normal camera behavior.
- Timing: 0–5 seconds hold, 5–20 left pass, 20–35 closer pass, 35–55 cross to the
  right, 55–70 pull back to the exact authored transform. The lens stays fixed.
  Position and look-at tracks use continuous cubic tangents, scaled to the
  authored overview and visible member bounds. Camera-channel sphere sweeps
  terminate blocked movement at the previous safe pose.
- WASD/QE, scene right-button look and scene wheel input cancel permanently at
  the current pose. Ordinary pointer motion and web panel wheel events do not.
  Detail/navigation/close, camera changes and application deactivation end the
  run. Only a fresh explicit Security entry starts another tour.
- `OntoTwin.Camera.OverviewTour` covers the trajectory and manual cancellation;
  `Tools/OntoTwinRestoration/verify_overview_tour.py` runs the real-map cycle,
  captures five views and checks completion, re-entry, detail, Back and close.
  It also sends keyboard/mouse events through PlayerController after completion,
  verifies movement, turning and stopping, and samples the live pulse colours.
- Input release is event-based on leaving the game viewport or deactivating the
  application. Returning focus must preserve the new press; no per-frame key
  flushing is allowed. Closing the Dock explicitly focuses the scene, and a
  completed tour restores scene focus when the pointer is outside the web panel.
  `OntoTwin.Camera.FocusHandoff` tests Slate focus changes with a real viewport.
- Communication status polling and page-driven updates use the same green /
  orange / red palette. The legacy `blue` payload field and `_Blue` asset suffix
  still identify the normal group for compatibility; its rendered colour is green.

When a business uses `isolate_focus`, its unfiltered overview first looks for one
`ACameraActor` tagged `OntoTwinViewForBusiness=<business_view_id>`. The tag is saved
in the level and survives cooking. Actor labels are used only by the binding tool.
The saved camera transform, FOV, aspect ratio and aspect-ratio constraint are used.
Missing or duplicate bindings fall back to automatic framing. Zone-filtered
businesses frame the selected zone instead of using the global overview.

The Security business (`bv.network-security`) is bound to `CameraActor_All` in
`Office_Demo_ArtMerged`. Run `Tools/OntoTwinRestoration/bind_business_camera.py`
inside the editor to bind the currently authored camera. It backs up the map and
records the lens parameters under `Saved/BusinessCameraBinding` before saving.
Changing that camera later updates the business's authored overview on next use.

Automatic framing uses the engine's FOV-axis rules and viewport aspect ratio. It
projects all eight corners of the combined visible member bounds, accounting for
depth and reserving 6% padding on each side. The web host maps the page's existing
`interactive_regions` from browser-local geometry into normalized viewport space,
including DPI and the native header. Framing uses the largest clear rectangle and
translates the camera so the target is centered in that rectangle. Full-screen
occlusion leaves the camera unchanged. Pages must mark their visible panels with
the bridge's interactive-region attributes; unreported overlays cannot be inferred.

On page entry, an automatic view can be corrected once when the first region
report arrives. Later region reports do not repeatedly move the user's camera.
Re-entering/focusing the business uses the latest layout. Authored default cameras
are never adjusted to fit panels. Highlight entry keeps the current camera, and
web-only entry does not trigger framing. Back restores the relevant scope policy.
Automatic views use a transient camera outside roaming mode, so they cannot move
an authored business or per-instance camera.

Security business environment policy:
- Entering `bv.network-security` requests actual unload/removal of
  `L_GrandNeoBay_Building`, `L_HaijuNorthBeta_Exterior`, `L_Campus500_Details` and
  `L_RefinedLandmarks`. This also works for their AlwaysLoaded streaming class.
- Strong references retain the original streaming descriptors and their flags;
  leaving Security reattaches them and restores their previous loading/visibility.
- The persistent satellite Landscape (and actors tagged `OntoTwinSatelliteMap`)
  is hidden with collision and ticking disabled. Previous states are restored.
- Office interior, devices, lights and runtime levels remain available. Drilling
  into a Security device keeps the exterior and satellite suppressed.

Security business background:
- The whole Security business uses the background derived from `Example_Map_11`
  (`HDR_FT_02`), including the follow intro, overview, manual movement and device
  details. Activation follows the same Security context as exterior suppression;
  it no longer depends on the camera transform. Leaving Security restores the
  original background states. No extra Sky Light is added.
- `M_SecurityQuietHDRI` samples a narrow cone in the HDRI's dark sky, reduces
  saturation and limits highlights so neon structures stay out of the background
  while the camera moves. The existing `MI_SecurityOverview_Example11` exposes
  `YawDegrees`, `PitchDegrees`, `ViewSpread`, `Brightness`, `Saturation` and
  `HighlightLimit` in the `Quiet Background` parameter group. The defaults are
  0 degrees yaw, 45 degrees pitch, 0.28 spread, 1.2 brightness, 0.9 saturation
  and 0.08 highlight limit. Shadow lifting, a blue-black base and a smooth
  highlight rolloff retain the HDRI texture instead of crushing it to black.
- The persistent actor tagged `OntoTwinSecurityOverviewBackground` starts hidden.
  Actors tagged `OntoTwinDefaultBackground` are hidden while Security is active,
  and their exact original hidden states are restored on exit. Lights and camera
  transforms are unchanged.
- `Tools/SecuritySky/configure_sky.py` configures the level actors, backing up maps
  before saving. `configure_dark_sky.py` creates the quiet material and backs up
  prior materials. Backups live under `Saved/SecuritySky/<timestamp>`.
- `Tools/SecuritySky/verify_sky.py` checks the real runtime switching sequence and
  saves overview, follow and detail captures under `Saved/SecuritySky`. It does
  not save runtime scene changes.

Validation:
- UE automation: `OntoTwin.Camera` (default binding and projection containment).
- Regression: `OntoTwin.WebInteraction.BusinessHighlight`.
- Environment state/restore: `OntoTwin.WebInteraction.SecurityEnvironment`.
- Real map: `verify_business_camera.py` with `-game -ExecCmds="py ..."` verifies
  saved binding, default/fallback/default switching, highlight and Back. Runtime
  tag removal is transient; the verification never saves the level.

Security entry sequence:
- Timing is read from `[Go2.SecurityBusinessIntro]` in `Config/DefaultGame.ini`
  at BeginPlay, overriding saved actor timing. Both delays start at follow entry.
- F7 retains the existing roaming entry behavior and route.
- Opening Security from the Dock blends to the scanner's F8 dog-follow camera
  in 0.8 seconds. At 8 seconds it requests one F6 scan; at 20 seconds it blends to the
  authored `CameraActor_All` business view (automatic framing remains the fallback).
- Leaving the business or manually switching camera modes cancels the pending
  scan and overview transitions. Re-entering Security restarts the sequence.
- `Go2_Tempo_OfficeRoute` has six stops at equal distances along its unchanged
  spline. Each stop waits for the body to settle, requests one scan, and holds
  for at least five seconds and until pending scans finish. Existing repeat
  behavior is retained. Overlapping automatic scan requests are queued.
- `Tools/Go2Scan/configure_security_stops.py` saves the six-stop configuration,
  verifies unchanged spline geometry and backs up the sublevel before saving.
- `Tools/Go2Scan/verify_security_sequence.py` checks the real-map intro, repeated
  entry, cancellation and a complete six-stop route using a separate game process.
  Its pause, repeat and time-dilation changes are transient.


