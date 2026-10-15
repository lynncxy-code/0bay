"""Turn a region painted red on a camera-view screenshot into that camera's image mask.

Usage (Python 3 with numpy and opencv-python):
    python mask_from_screenshot.py <camera id> <screenshot> [--camera-frame frame.png]

1. In the running scene press X until the camera's own view is shown (fusion can be on or off).
2. Take a screenshot of the whole 3D viewport (no window border), paint the area that should show the camera
   video in red (any semi-transparent red works), save it.
3. Run this. It writes Content/VideoTwin/mask_<id>.png; put "image_mask": "mask_<id>.png" under that camera in
   calibration.json and restart. With --camera-frame it also writes mask_<id>_preview.jpg next to the screenshot:
   the camera picture, dimmed where the model will stay (it shows real people: keep it out of the repository).

Why it works: the X view is a pinhole camera at the projector's calibrated position with the same horizontal FOV,
so every screen pixel maps to one point of the camera picture regardless of depth.

CAVEAT (2026-10-16): the X view is not aimed exactly like the projector. For camera 17 it was measured at
(-0.08, -0.07) of the picture (shader uv = this mapping + that), most likely a rotation on the level camera's
own camera component that the runtime does not reset. Pass it with --view-offset; measure it for other cameras
first (compare one X-view frame with and without the mask, or the video in the X view against a camera frame). The mapping below is the one in
projection.hlsl (normalised 16:9 picture, principal point, k1/k2 barrel distortion).

Where the painted region touches the screenshot's edge it is carried on to the edge of the camera picture (the
camera sees more than the undistorted view). The edge is softened by about 1.5 % of the picture width, and the PNG
is written sRGB-encoded because the runtime samples it as an sRGB texture.
"""
import argparse
import json
import math
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
MW, MH = 1024, 576


def red_region(shot):
    b, g, r = [shot[:, :, i].astype(np.int32) for i in range(3)]
    red = ((r - np.maximum(g, b)) > 35).astype(np.uint8) * 255
    red = cv2.morphologyEx(red, cv2.MORPH_OPEN, np.ones((5, 5), np.uint8))      # small red things in the scene
    red = cv2.morphologyEx(red, cv2.MORPH_CLOSE, np.ones((25, 25), np.uint8))   # dark chairs inside the paint
    n, lab, st, _ = cv2.connectedComponentsWithStats(red)
    if n < 2:
        raise SystemExit('no red region found')
    red = np.where(lab == 1 + np.argmax(st[1:, cv2.CC_STAT_AREA]), 255, 0).astype(np.uint8)
    h, w = red.shape
    ff = red.copy()
    cv2.floodFill(ff, np.zeros((h + 2, w + 2), np.uint8), (0, 0), 255)
    return red | cv2.bitwise_not(ff)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('camera')
    ap.add_argument('screenshot')
    ap.add_argument('--camera-frame', help='a picture from the camera, for the preview')
    ap.add_argument('--view-offset', nargs=2, type=float, default=[0.0, 0.0], metavar=('DU', 'DV'),
                    help='measured offset of the X view in picture units (camera 17: -0.08 -0.07)')
    a = ap.parse_args()
    cam = json.loads((ROOT / 'Content/VideoTwin/calibration.json').read_text(encoding='utf-8'))['cameras'][a.camera]
    hfov, k1, k2 = cam['horizontal_fov_deg'], cam.get('k1', 0.0), cam.get('k2', 0.0)
    ppx, ppy = cam.get('principal_point', [0.5, 0.5])
    aspect = 16 / 9
    shot = cv2.imread(a.screenshot)
    red = red_region(shot)
    h, w = red.shape
    t = math.tan(math.radians(hfov / 2))
    u, v = np.meshgrid((np.arange(MW) + 0.5) / MW, (np.arange(MH) + 0.5) / MH)
    du, dv = a.view_offset
    qdx, qdy = (u - du - ppx) / (0.5 / t), (v - dv - ppy) / (0.5 * aspect / t)
    qx, qy = qdx.copy(), qdy.copy()
    for _ in range(30):                     # undo the radial distortion
        r2 = qx * qx + qy * qy
        rad = 1 + k1 * r2 + k2 * r2 * r2
        qx, qy = qdx / rad, qdy / rad
    sx = ((qx / t + 1) / 2 * w).astype(np.float32)
    sy = ((qy / (t * h / w) + 1) / 2 * h).astype(np.float32)
    m = cv2.remap(red, sx, sy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    m = (m > 127).astype(np.uint8) * 255
    lin = cv2.GaussianBlur(m, (0, 0), sigmaX=6).astype(np.float32) / 255
    enc = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(lin, 1 / 2.4) - 0.055)
    out = ROOT / f'Content/VideoTwin/mask_{a.camera}.png'
    # Three channels, not grey: a grey PNG becomes a one-channel texture, and that format has no sRGB decode.
    g8 = np.clip(enc * 255 + 0.5, 0, 255).astype(np.uint8)
    cv2.imwrite(str(out), cv2.merge([g8, g8, g8]))
    print(f'{out}: {m.mean() / 255:.0%} of the picture shows video')
    if a.camera_frame:
        frame = cv2.resize(cv2.imread(a.camera_frame), (MW, MH)).astype(np.float32)
        k = lin[..., None]
        # Next to the screenshot, never under Content/: it shows real people and Content/VideoTwin is packaged.
        prev = Path(a.screenshot).resolve().with_name(f'mask_{a.camera}_preview.jpg')
        cv2.imwrite(str(prev), (frame * k + frame * 0.35 * (1 - k)).astype(np.uint8))
        print(f'{prev}: bright = video, dim = model')


if __name__ == '__main__':
    main()
