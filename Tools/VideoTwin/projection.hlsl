// World-position reprojection. This is not a screen-aligned video overlay.
float3 delta = WorldPos - ProjectorPosition;
float z = dot(delta, ProjectorForward);
float2 q = float2(dot(delta, ProjectorRight), -dot(delta, ProjectorUp)) / max(z, 0.001);
float2 idealUV = q * float2(0.5 / Lens.x, 0.5 * Lens.y / Lens.x) + Lens.zw;
float r2 = dot(q, q);
float radial = 1.0 + Distortion.x * r2 + Distortion.y * r2 * r2;
float2 uv = q * radial * float2(0.5 / Lens.x, 0.5 * Lens.y / Lens.x) + Lens.zw;
float inFrustum = (z > 1.0 && z < Options.z && all(idealUV > 0) && all(idealUV < 1) && all(uv > 0) && all(uv < 1)) ? 1.0 : 0.0;
float inBounds = (all(WorldPos >= ReceiverMin) && all(WorldPos <= ReceiverMax)) ? 1.0 : 0.0;
// Soft border on the box's four sides (Display.w cm wide, 0 = hard). The box's floor and ceiling stay hard:
// the floor sits a few cm above the box bottom and would otherwise be faded out.
float2 inside = min(WorldPos.xy - ReceiverMin.xy, ReceiverMax.xy - WorldPos.xy);
float border = Display.w > 0.5 ? smoothstep(0.0, Display.w, min(inside.x, inside.y)) : 1.0;
float storedDepth = Texture2DSampleLevel(ProjectorDepthTex, ProjectorDepthTexSampler, saturate(idealUV), 0).r;
// Conservative 1-texel visibility dilation avoids false rejection at thin edges
// where the main temporal-upscaled view and the projector raster differ.
[unroll] for (int sy=-1; sy<=1; ++sy)
{
    [unroll] for (int sx=-1; sx<=1; ++sx)
        storedDepth=max(storedDepth,Texture2DSampleLevel(ProjectorDepthTex,ProjectorDepthTexSampler,
            saturate(idealUV+float2(sx/1920.0,sy/1080.0)),0).r);
}
float tolerance = max(Options.y, z * 0.002);
// Soft occlusion: full video up to the tolerance, fading out over half as much again, instead of a hard cut
// that leaves a ragged edge wherever the model and the room disagree by a few centimetres.
float visible = storedDepth > 1 ? 1.0 - smoothstep(storedDepth + tolerance, storedDepth + tolerance * 1.5, z) : 0.0;
// A surface that faces away from the projector, or that it only grazes, never takes its video. On a back face
// the picture is not what the camera saw (the end of a cabinet the camera looks past showed up as a floating
// grey block from the opposite camera); on a grazing face a few source pixels get stretched into long streaks.
// The normal is the geometric one, from the reconstructed positions of neighbouring pixels. The GBuffer normal
// carries the carpet and fabric normal maps: with it the test flipped pixel by pixel and peppered the floor
// with black specks. Turned towards the viewer (every visible surface faces the viewer); across a depth edge
// the neighbours belong to different surfaces, so the test is skipped there instead of guessing. The threshold
// only catches back faces and near-tangent grazing: some materials offset pixel depth, which roughens the
// reconstructed normal, and a 0.06 cut-off still dotted the far floor (which the camera sees at 6+ degrees).
float3 dpx = ddx(WorldPos);
float3 dpy = ddy(WorldPos);
float3 toEye = CameraPos - WorldPos;
float eyeDist = max(length(toEye), 1.0);
float3 gn = cross(dpx, dpy);
float gnLen = length(gn);
float facing = 1.0;
if (gnLen > 1e-6 && max(length(dpx), length(dpy)) < 0.03 * eyeDist)
{
    gn /= gnLen;
    gn *= dot(gn, toEye) >= 0 ? 1.0 : -1.0;
    facing = dot(gn, normalize(ProjectorPosition - WorldPos));
}
float facingMask = smoothstep(-0.02, 0.02, facing);
// Options.w: 0 = no occlusion, 1 = occlusion, 2 = this camera's own view. From the camera itself the projected
// picture is the camera picture on whatever surface it lands, so neither test is needed there: the video shows
// whole inside the receiver box and only the box edge decides where it ends.
float ownView = Options.w > 1.5 ? 1.0 : 0.0;
float useOcclusion = (Options.w > 0.5 && Options.w < 1.5) ? 1.0 : 0.0;
facingMask = lerp(facingMask, 1.0, ownView);
float edge = min(min(uv.x, 1-uv.x), min(uv.y, 1-uv.y));
// Hand-drawn region in the camera's own picture ("image_mask" in calibration.json, white = video, soft edge drawn in).
// A box edge cuts through whatever stands on it; a line drawn along real edges in the picture does not.
// Cameras without a mask get a white texture.
float4 maskTexel = Texture2DSampleLevel(MaskTex, MaskTexSampler, saturate(uv), 0);
// Distortion.z = 1 ("image_mask_fill", 2026-10-16): the mask texture is a stretch map instead. Red = horizontal offset
// to sample the video at, (red*255 - 128) * 2 picture pixels of 1024; alpha is the mask (normally all white). Around
// the real planter cabinets the video on both sides is stretched over them, so neither the cabinet nor the model's
// furniture behind it shows, and the picture stays live (a still patch looked blurred). Without it: mask in red.
float fillMode = step(0.5, Distortion.z);
float userMask = lerp(maskTexel.r, maskTexel.a, fillMode);
// Furniture that stays the model whatever camera looks at it ("exclude_boxes" in calibration.json, up to 12 world
// boxes; unused ones are set inside-out so nothing is ever in them). ExcludeMinN.w = 1 ("green_only"): inside that box
// only pixels the model draws in plant green stay the model. 2026-10-16: the plants on the planter cabinets, which
// overlap the runtime desks, partitions and chairs in the model -- a box alone cannot tell them apart, colour can
// (leaves are saturated green; the partition fabric is a greyish green, desks white, chairs dark grey).
float plantGreen = smoothstep(0.03, 0.08, SceneColor.g - max(SceneColor.r, SceneColor.b));
float excluded = 0.0;
excluded = max(excluded, ((all(WorldPos >= ExcludeMin0.xyz) && all(WorldPos <= ExcludeMax0.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin0.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin1.xyz) && all(WorldPos <= ExcludeMax1.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin1.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin2.xyz) && all(WorldPos <= ExcludeMax2.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin2.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin3.xyz) && all(WorldPos <= ExcludeMax3.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin3.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin4.xyz) && all(WorldPos <= ExcludeMax4.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin4.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin5.xyz) && all(WorldPos <= ExcludeMax5.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin5.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin6.xyz) && all(WorldPos <= ExcludeMax6.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin6.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin7.xyz) && all(WorldPos <= ExcludeMax7.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin7.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin8.xyz) && all(WorldPos <= ExcludeMax8.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin8.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin9.xyz) && all(WorldPos <= ExcludeMax9.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin9.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin10.xyz) && all(WorldPos <= ExcludeMax10.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin10.w)));
excluded = max(excluded, ((all(WorldPos >= ExcludeMin11.xyz) && all(WorldPos <= ExcludeMax11.xyz)) ? 1.0 : 0.0) * lerp(1.0, plantGreen, step(0.5, ExcludeMin11.w)));
float showMask = lerp(userMask, 1.0, fillMode) * (1.0 - excluded);
float mask = inFrustum * inBounds * border * facingMask * showMask * smoothstep(0.0, 0.012, edge);
mask *= lerp(1.0, visible, useOcclusion);
if (Display.z > 0.5 && uv.x < 0.35 && uv.y < 0.14) mask = 0;
if (Display.x > 0.5 && ScreenUV.x > 0.5) mask = 0;
float stretchEnc = maskTexel.r;
stretchEnc = lerp(stretchEnc * 12.92, 1.055 * pow(max(stretchEnc, 1e-5), 1.0 / 2.4) - 0.055, step(0.0031308, stretchEnc));
float2 sampleUV = uv + float2(fillMode * (stretchEnc * 255.0 - 128.0) * 2.0 / 1024.0, 0.0);
float3 video = Texture2DSampleLevel(VideoTex, VideoTexSampler, saturate(sampleUV), 0).rgb;
// This material runs after tonemapping, where scene colour is already display-encoded, but the sampler
// returns linear values. Written as-is the picture came out a gamma step too dark (measured: source
// mid-grey 154 shown as 84). Encode to sRGB so the video shows at its own brightness.
video = lerp(video * 12.92, 1.055 * pow(max(video, 1e-5), 1.0 / 2.4) - 0.055, step(0.0031308, video));
if (Display.y > 0.5)
{
    float3 coverage = lerp(float3(1,0.08,0.02),float3(0.03,0.9,0.35),visible * facingMask);
    return lerp(SceneColor.rgb, coverage, inFrustum * inBounds * 0.7 * Options.x);
}
float blend = saturate(mask * Options.x);
// Never let a bad value reach the screen as a black or white pixel.
if (!(blend >= 0.0)) blend = 0.0;
return lerp(SceneColor.rgb, video, blend);
