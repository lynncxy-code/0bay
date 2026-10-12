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
float visible = (z <= storedDepth + tolerance && storedDepth > 1) ? 1.0 : 0.0;
float edge = min(min(uv.x, 1-uv.x), min(uv.y, 1-uv.y));
float mask = inFrustum * inBounds * smoothstep(0.0, 0.012, edge);
mask *= lerp(1.0, visible, Options.w);
if (Display.z > 0.5 && uv.x < 0.35 && uv.y < 0.14) mask = 0;
if (Display.x > 0.5 && ScreenUV.x > 0.5) mask = 0;
float3 video = Texture2DSampleLevel(VideoTex, VideoTexSampler, saturate(uv), 0).rgb;
// This material runs after tonemapping, where scene colour is already display-encoded, but the sampler
// returns linear values. Written as-is the picture came out a gamma step too dark (measured: source
// mid-grey 154 shown as 84). Encode to sRGB so the video shows at its own brightness.
video = lerp(video * 12.92, 1.055 * pow(max(video, 1e-5), 1.0 / 2.4) - 0.055, step(0.0031308, video));
if (Display.y > 0.5)
{
    float3 coverage = lerp(float3(1,0.08,0.02),float3(0.03,0.9,0.35),visible);
    return lerp(SceneColor.rgb, coverage, inFrustum * inBounds * 0.7 * Options.x);
}
return lerp(SceneColor.rgb, video, saturate(mask * Options.x));
