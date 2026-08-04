// shaders.h - HLSL sources for the XMB background pipelines + gradient.
//
// The six pipeline shaders are ported verbatim from RetroArch
// (gfx/drivers/d3d_shaders/*_sm4.hlsl.h, GPLv3). The ONLY change vs upstream is the
// uniform wrapper: RetroArch's `uniform UBO global;` is expressed here as
// `cbuffer GlobalCB : register(b0) { UBO global; };`, which keeps every `global.xxx`
// reference in the bodies unchanged while binding cleanly on D3D11. Do not "clean up"
// the bodies - fidelity to RetroArch's look depends on them being byte-identical.
//
// This project is therefore GPLv3 (see LICENSE).
#pragma once

// ------- shared cbuffer preamble (ribbon variant: has Outputsize + alpha) -------
static const char* kRibbonCommon = R"HLSL(
struct UBO
{
   float4x4 modelViewProj;
   float2 Outputsize;
   float time;
   float alpha;
};
cbuffer GlobalCB : register(b0) { UBO global; };

float iqhash(float n) { return frac(sin(n) * 43758.5453); }
float noise(float3 x)
{
   float3 p = floor(x);
   float3 f = frac(x);
   f = f * f * (3.0 - 2.0 * f);
   float n = p.x + p.y * 57.0 + 113.0 * p.z;
   return lerp(lerp(lerp(iqhash(n), iqhash(n + 1.0), f.x),
              lerp(iqhash(n + 57.0), iqhash(n + 58.0), f.x), f.y),
              lerp(lerp(iqhash(n + 113.0), iqhash(n + 114.0), f.x),
              lerp(iqhash(n + 170.0), iqhash(n + 171.0), f.x), f.y), f.z);
}
float xmb_noise2(float3 x)
{
   return cos(x.z * 4.0) * cos(x.z + global.time / 10.0 + x.x);
}
)HLSL";

// ------- MENU: modern ribbon -------
static const char* kRibbonBody = R"HLSL(
struct PSInput
{
   float4 position : SV_POSITION;
   float3 vEC      : TEXCOORD;
};
PSInput VSMain(float2 position : POSITION)
{
   float3 v = float3(position.x, 0.0, 1.0-position.y);
   float3 v2 = v;
   float3 v3 = v;
   v.y = xmb_noise2(v2) / 8.0;
   v3.x -= global.time / 5.0;
   v3.x /= 4.0;
   v3.z -= global.time / 10.0;
   v3.y -= global.time / 100.0;
   v.z -= noise(v3 * 7.0) / 15.0;
   v.y -= noise(v3 * 7.0) / 15.0 + cos(v.x * 2.0 - global.time / 2.0) / 5.0 - 0.3;
   v.y = -v.y;
   PSInput output;
   output.position = float4(v.xy, 0.0, 1.0);
   output.vEC = v;
   return output;
}
float4 PSMain(PSInput input) : SV_TARGET
{
   const float3 up = float3(0.0, 0.0, 1.0);
   float3 x = ddx(input.vEC);
   float3 y = ddy(input.vEC);
   float3 normal = normalize(cross(x, y));
   float c = 1.0 - dot(normal, up);
   c = (1.0 - cos(c * c)) / 13.0;
   return float4(c, c, c, global.alpha);
}
)HLSL";

// ------- MENU: custom colour ribbon (same geometry as modern ribbon, user-coloured) -------
// Colour + opacity come from a second constant buffer (b1), set from C++ (gRibbonColor /
// gRibbonOpacity); a settings UI will drive those later. ALPHA blend = translucent, so it shows
// on a pure-black background without blowing out. Opacity curve: alpha = saturate(c * uOpacity),
// where c is the fold sheen - higher = more solid, lower = fainter/more translucent.
static const char* kRibbonCustomBody = R"HLSL(
cbuffer RibbonCB : register(b1) { float4 uRibbonColor; float uRibbonOpacity; float uRibbonSheen; float2 _rpad; };
struct PSInput
{
   float4 position : SV_POSITION;
   float3 vEC      : TEXCOORD;
};
PSInput VSMain(float2 position : POSITION)
{
   float3 v = float3(position.x, 0.0, 1.0-position.y);
   float3 v2 = v;
   float3 v3 = v;
   v.y = xmb_noise2(v2) / 8.0;
   v3.x -= global.time / 5.0;
   v3.x /= 4.0;
   v3.z -= global.time / 10.0;
   v3.y -= global.time / 100.0;
   v.z -= noise(v3 * 7.0) / 15.0;
   v.y -= noise(v3 * 7.0) / 15.0 + cos(v.x * 2.0 - global.time / 2.0) / 5.0 - 0.3;
   v.y = -v.y;
   PSInput output;
   output.position = float4(v.xy, 0.0, 1.0);
   output.vEC = v;
   return output;
}
float4 PSMain(PSInput input) : SV_TARGET
{
   const float3 up = float3(0.0, 0.0, 1.0);
   float3 x = ddx(input.vEC);
   float3 y = ddy(input.vEC);
   float3 normal = normalize(cross(x, y));
   float c = 1.0 - dot(normal, up);
   c = (1.0 - cos(c * c)) / uRibbonSheen;        // fold sheen (divisor is a user setting; lower = stronger)
   return float4(uRibbonColor.rgb, saturate(c * uRibbonOpacity));   // translucent, user-coloured
}
)HLSL";

// ------- MENU_2: simple ribbon -------
static const char* kRibbonSimpleBody = R"HLSL(
float4 VSMain(float2 position : POSITION) : SV_POSITION
{
   float3 v = float3(position.x, 0.0, position.y);
   float3 v2 = v;
   v2.x = v2.x + global.time / 2.0;
   v2.z = v.z * 3.0;
   v.y = cos((v.x + v.z / 3.0 + global.time) * 2.0) / 10.0 + noise(v2.xyz) / 4.0;
   v.y = -v.y;
   return float4(v.xy, 0.0, 1.0);
}
float4 PSMain() : SV_TARGET
{
   return float4(0.05, 0.05, 0.05, global.alpha);
}
)HLSL";

// ------- quad-based effects (snow / bokeh / snowflake): OutputSize, no alpha -------
static const char* kQuadCommon = R"HLSL(
struct UBO
{
   float4x4 modelViewProj;
   float2 OutputSize;
   float time;
};
cbuffer GlobalCB : register(b0) { UBO global; };

struct PSInput
{
   float4 position : SV_POSITION;
   float2 texcoord : TEXCOORD0;
};
PSInput VSMainTex(float4 position : POSITION, float2 texcoord : TEXCOORD0)
{
   PSInput result;
   result.position = mul(global.modelViewProj, position);
   result.texcoord = texcoord;
   return result;
}
float4 VSMainPos(float4 position : POSITION, float2 texcoord : TEXCOORD0) : SV_POSITION
{
   return mul(global.modelViewProj, position);
}
)HLSL";

// MENU_3 simple_snow / MENU_4 snow differ only in the three static consts below.
static const char* kSnowBody = R"HLSL(
static const float baseScale = %BASESCALE%;
static const float density   = %DENSITY%;
static const float speed     = %SPEED%;

float rand(float2 co) { return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453); }
float dist_func(float2 distv)
{
   float dist = sqrt((distv.x * distv.x) + (distv.y * distv.y)) * (40.0 / baseScale);
   dist = clamp(dist, 0.0, 1.0);
   return cos(dist * (3.14159265358 * 0.5)) * 0.5;
}
float random_dots(float2 co)
{
   float part = 1.0 / 20.0;
   float2 cd = floor(co / part);
   float p = rand(cd);
   if (p > 0.005 * (density * 40.0)) return 0.0;
   float2 dpos = (float2(frac(p * 2.0), p) + float2(2.0, 2.0)) * 0.25;
   float2 cellpos = frac(co / part);
   float2 distv = (cellpos - dpos);
   return dist_func(distv);
}
float snow(float2 pos, float time, float scale)
{
   pos.x += cos(pos.y * 1.2 + time * 3.14159 * 2.0 + 1.0 / scale) / (8.0 / scale) * 4.0;
   // add gravity (continuous fall). RetroArch master wraps this with frac(.../0.05)
   // which snaps flakes back every grid cell and breaks the falling motion; we use
   // the classic continuous scroll. time is bounded (wraps at 65536) so precision is fine.
   pos += time * scale * float2(-0.5, 1.0) * 4.0;
   return random_dots(pos / scale) * (scale * 0.5 + 0.5);
}
float4 PSMain(PSInput input) : SV_TARGET
{
   float tim = global.time * 0.4 * speed;
   float2 pos = input.position.xy / global.OutputSize.xx;
   pos.y = 1.0 - pos.y;
   float a = 0.0;
   a += snow(pos, tim, 1.0);
   a += snow(pos, tim, 0.7);
   a += snow(pos, tim, 0.6);
   a += snow(pos, tim, 0.5);
   a += snow(pos, tim, 0.4);
   a += snow(pos, tim, 0.3);
   a += snow(pos, tim, 0.25);
   a += snow(pos, tim, 0.125);
   a = a * min(pos.y * 4.0, 1.0);
   return float4(1.0, 1.0, 1.0, a);
}
)HLSL";

// MENU_5 bokeh
static const char* kBokehBody = R"HLSL(
float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
   float speed = global.time * 4.0;
   float2 uv = -1.0 + 2.0 * position.xy / global.OutputSize;
   uv.x *= global.OutputSize.x / global.OutputSize.y;
   float3 color = float3(0.0, 0.0, 0.0);
   for (int i = 0; i < 8; i++)
   {
      float pha = sin(float(i) * 546.13 + 1.0) * 0.5 + 0.5;
      float siz = pow(sin(float(i) * 651.74 + 5.0) * 0.5 + 0.5, 4.0);
      float pox = sin(float(i) * 321.55 + 4.1) * global.OutputSize.x / global.OutputSize.y;
      float rad = 0.1 + 0.5 * siz + sin(pha + siz) / 4.0;
      float2 pos = float2(pox + sin(speed / 15. + pha + siz), -1.0 - rad + (2.0 + 2.0 * rad) * frac(pha + 0.3 * (speed / 7.) * (0.2 + 0.8 * siz)));
      float dis = length(uv - pos);
      if (dis < rad)
      {
         float3 col = lerp(float3(0.194 * sin(speed / 6.0) + 0.3, 0.2, 0.3 * pha), float3(1.1 * sin(speed / 9.0) + 0.3, 0.2 * pha, 0.4), 0.5 + 0.5 * sin(float(i)));
         color += col.zyx * (1.0 - smoothstep(rad * 0.15, rad, dis));
      }
   }
   color *= sqrt(1.5 - 0.5 * length(uv));
   return float4(color.r, color.g, color.b, 0.5);
}
)HLSL";

// ------- gradient background (our own; renders the color theme) -------
static const char* kGradientShader = R"HLSL(
struct VSIn  { float2 pos : POSITION; float4 col : COLOR; };
struct PSIn  { float4 position : SV_POSITION; float4 col : COLOR; };
PSIn VSMain(VSIn i) { PSIn o; o.position = float4(i.pos, 0.0, 1.0); o.col = i.col; return o; }
float4 PSMain(PSIn i) : SV_TARGET { return i.col; }
)HLSL";
