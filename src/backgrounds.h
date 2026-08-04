// backgrounds.h - original + ported animated background pipelines for Launcher.
//
// Each is an HLSL pixel shader over a fullscreen quad. In main.cpp we compile each as
//   std::string(kQuadCommon) + kNoise + <body>
// so every body can assume: cbuffer UBO global {mvp, OutputSize, time}; a fullscreen VS
// entry "VSMainPos"; and the shared noise helpers below (hsh/hsh3/vnoise/fbm/aspectX/gmod).
// Bodies define ONLY PSMain(float4 position : SV_POSITION) : SV_TARGET and read
// global.time / global.OutputSize. position.xy = pixel coordinates (origin TOP-left).
//
// Ports from Shadertoy (GLSL): fragCoord origin is BOTTOM-left, so we flip Y
// (frag = float2(x, OutputSize.y - y)) before feeding the ported math. GLSL->HLSL notes:
// vec*->float*, mat2->float2x2, fract->frac, mix->lerp, mod-> gmod (GLSL mod handles
// negatives; HLSL fmod truncates), iTime->global.time, iResolution->global.OutputSize.
// NOTE: we have NO input textures - Shadertoy shaders using iChannel/texture() must have those
// lookups replaced with procedural noise (hsh/vnoise/fbm) before they can be ported.
//
// HLSL gotcha: do NOT name identifiers after reserved words (line, point, triangle, vector,
// matrix, sample, sampler, texture, pixel, vertex, in/out/inout, linear, precise, dword...).
// Also do NOT redefine the kNoise helpers (fbm/vnoise/hsh...) inside a body - prefix instead.
//
// See docs/BACKGROUNDS.md for the catalogue.
#pragma once

// -------- shared helpers (prepended to every background) --------
static const char* kNoise = R"HLSL(
float  hsh(float2 p){ return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float3 hsh3(float2 p){ return frac(sin(float3(dot(p,float2(127.1,311.7)),
                       dot(p,float2(269.5,183.3)), dot(p,float2(419.2,371.9)))) * 43758.5453); }
float vnoise(float2 p)
{
   float2 i = floor(p), f = frac(p);
   f = f * f * (3.0 - 2.0 * f);
   float a = hsh(i), b = hsh(i + float2(1,0)), c = hsh(i + float2(0,1)), d = hsh(i + float2(1,1));
   return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
float fbm(float2 p)
{
   float s = 0.0, amp = 0.5;
   for (int i = 0; i < 5; i++){ s += amp * vnoise(p); p *= 2.0; amp *= 0.5; }
   return s;
}
float aspectX(){ return global.OutputSize.x / global.OutputSize.y; }
float gmod(float x, float y){ return x - y * floor(x / y); }   // GLSL-style mod (handles negatives)
)HLSL";

// 1. Underwater Caustics
static const char* kBgCaustics = R"HLSL(
float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
   float2 uv = position.xy / global.OutputSize; uv.x *= aspectX();
   float t = global.time * 0.35;
   float2 p = uv * 8.0;
   float c = pow(saturate(1.0 - abs(sin(p.x + sin(p.y + t)) * cos(p.y + cos(p.x - t)))), 3.0);
   float3 col = lerp(float3(0.0, 0.1, 0.2), float3(0.3, 0.85, 1.0), c);
   return float4(col, 0.5 + 0.5 * c);
}
)HLSL";

// 2. Synthwave Road - sunset, distant buildings, bending neon-glow grid road.
//    Ported from Shadertoy "7ltcRn" (technique-inspired). No loops; cheap.
static const char* kBgSynthRoad = R"HLSL(
static const float3 SR_sunsetUp   = float3(1.0, 0.59, 0.32);
static const float3 SR_sunsetDown = float3(0.58, 0.11, 0.44);
static const float3 SR_sunUp      = float3(1.0, 0.9, 0.0);
static const float3 SR_sunDown    = float3(0.75, 0.21, 0.44);
static const float3 SR_gridColor  = float3(0.8, 0.5, 0.75);
static const float3 SR_gridColor2 = float3(0.59, 0.05, 0.45);
static const float3 SR_ground     = float3(0.24, 0.11, 0.26);
static const float3 SR_road       = float3(0.05, 0.05, 0.05);
static const float3 SR_road2      = float3(0.1, 0.1, 0.1);
static const float3 SR_paint      = float3(0.05, 0.05, 0.05);
static const float3 SR_paint2     = float3(1.0, 1.0, 0.33);
static const float3 SR_center     = float3(1.0, 1.0, 0.33);
static const float SR_sunSize = 0.7, SR_stripeSize = 0.05, SR_stripeOff = 0.04;
static const float2 SR_sunPos = float2(0.0, 0.1);
static const float SR_horizonY = -0.24, SR_roadScale = -0.2, SR_speed = 20.0, SR_bend = 0.1;
static const float SR_roadWidth = 2.8, SR_roadDetail = 4.0, SR_zoom = 0.05;
static const float SR_bScroll = 20.0, SR_bHeight = 0.15, SR_bWidth = 25.0;
static const float SR_sideW = 0.2, SR_centerW = 0.15, SR_gridW = 0.1;
static const float SR_glowInt = 1.2, SR_glowRad = 0.1, SR_glowFac = 0.4;
float SR_h21(float2 p){ return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
float4 SR_sky(float2 p, float t)
{
   float sd = sin(60.0 * p.y + t * 2.0) * 0.002;
   float sun = length(p + float2(sd, sd) - SR_sunPos);
   float3 skyColor = lerp(SR_sunsetDown, SR_sunsetUp, p.y);
   float stripe = gmod(t * 0.02 + SR_stripeOff + (p.y - SR_horizonY) * (p.y - SR_horizonY), SR_stripeSize);
   float3 ret = ((sun < SR_sunSize) && (stripe > 0.01))
              ? lerp(SR_sunDown, SR_sunUp, (p.y + SR_sunPos.y) / SR_sunSize) : skyColor;
   float scroll = SR_bScroll * cos(SR_bend * t);
   float h = SR_h21(floor(float2(scroll, scroll) + float2(p.x, p.x) * SR_bWidth)) * SR_bHeight;
   ret = (p.y - SR_horizonY < h) ? float3(0.0, 0.0, 0.0) : ret;
   return float4(ret, 1.0);
}
float4 SR_roadf(float2 p, float t)
{
   float3 q = float3(p, 1.0) / (SR_roadScale - p.y);
   float refl = 0.8 * (1.0 - abs(p.y - SR_horizonY));
   refl = refl * refl * refl;
   float k = SR_zoom * sin(SR_bend * t);
   float w = abs(q.x + k * q.z * q.z);
   float road = sin(SR_roadDetail * q.z + SR_speed * t);
   float3 c;
   if (w > SR_roadWidth)
   {
      bool vGrid = gmod(w - SR_roadWidth, 1.0) > (1.0 - SR_gridW);
      bool hGrid = (road < 0.0 && road > -SR_gridW * 3.0);
      float3 grid = (vGrid || hGrid) ? lerp(SR_gridColor, SR_gridColor2, refl) : SR_ground;
      float blend = 1.0 - abs(p.y - SR_horizonY);
      c = lerp(grid, SR_ground, blend * blend);
   }
   else
   {
      if (road > 0.0)
         c = (w > (SR_roadWidth - SR_sideW)) ? SR_paint : SR_road;
      else
         c = (w > (SR_roadWidth - SR_sideW)) ? SR_paint2
           : ((w > (SR_centerW * 0.5)) ? SR_road2 : SR_center);
      float4 invSky = SR_sky(-p + float2(sin(30.0 * p.y + t * 2.0) * 0.01, SR_horizonY), t);
      c = invSky.rgb * refl + c * (1.0 - refl);
   }
   float d = abs(w - SR_roadWidth);
   d = min(d, w);
   d = max(d, road);
   return float4(c, d);
}
float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
   float2 res = global.OutputSize;
   float2 frag = float2(position.x, res.y - position.y);   // flip to GL orientation
   float2 p = ((frag - 0.5 * res) / res.y) * 2.0;
   float t = global.time;
   float3 c;
   if (p.y > SR_horizonY)
   {
      c = SR_sky(p, t).rgb;
   }
   else
   {
      float4 skyC = SR_sky(p, t);
      float4 rdC  = SR_roadf(p, t);
      float3 glowColor = (skyC.a < rdC.a) ? SR_sunDown : SR_paint2;
      float a = min(skyC.a, rdC.a);
      c = rdC.rgb;
      float glow = pow(SR_glowRad / a, SR_glowInt);
      c += glow * glowColor * SR_glowFac;
   }
   return float4(c, 1.0);
}
)HLSL";

// 3. Orbs - two soft glowing orbs (cyan + magenta) orbiting on black.
//    Ported from Shadertoy (technique-inspired). No loops; very cheap.
static const char* kBgOrbs = R"HLSL(
float obSmoother(float a, float b, float x){ x = clamp((x - a) / (b - a), 0.0, 1.0); return x * x * x * (x * (x * 6.0 - 15.0) + 10.0); }
// Fully-saturated HSV->RGB so the orbs stay neon-vivid while their hue drifts over time.
float3 obHue(float h){ float3 p = abs(frac(h.xxx + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0); return clamp(p - 1.0, 0.0, 1.0); }
float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
   float2 res = global.OutputSize;
   float2 uv = (position.xy * 2.0 - res) / res.y;
   float t = global.time;
   const float PI = 3.14159265358979;
   const float radius = 2.0;
   // Slowly cycle both orbs through the spectrum, kept complementary (half a turn apart). ~0.03 = a full
   // rainbow every ~33s; defaults to the old cyan/magenta pair near t = 0.
   float hue = frac(t * 0.03 + 0.5);
   float3 color1 = obHue(hue);
   float3 color2 = obHue(frac(hue + 0.5));
   float2 rotate1 = float2(cos(t) * 1.5, sin(2.0 * t)) * 0.5;
   float2 rotate2 = float2(cos(t + PI) * 1.5, sin(2.0 * t + PI)) * 0.5;
   float g1 = obSmoother(radius, -radius, length(uv - rotate1));
   float g2 = obSmoother(radius, -radius, length(uv - rotate2));
   float3 col = g1 * color1 + g2 * color2;   // color0 (background) is black -> no extra term
   return float4(col, 1.0);
}
)HLSL";
