export const INTERIOR_ACES_EXPOSURE = 1.12;
export const INTERIOR_ENV_INTENSITY = 0.55;
export const INTERIOR_HEMI = 0.58;
export const INTERIOR_SUN = 0.78;
export const INTERIOR_FILL = 0.22;
export const ADULT_EYE_HEIGHT_M = 1.72;
export const MIN_STANDING_VIEWER_Y = 1.62;

export function xrEyeLiftY(spaceType, viewerY, standingHeight = ADULT_EYE_HEIGHT_M) {
  if (viewerY == null || Number.isNaN(viewerY)) {
    return spaceType === "local-floor" ? 0 : standingHeight;
  }
  return Math.max(0, standingHeight - viewerY);
}
