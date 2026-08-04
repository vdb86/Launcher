# RetroArch XMB pipeline - port notes

Source: libretro/RetroArch (master), GPLv3. These notes capture exactly how XMB drives
its background shader pipelines on the D3D11 backend, so we can reproduce it 1:1.

## Pipeline slots (VIDEO_SHADER_MENU .. _6)
| Slot | Shader (d3d_shaders/*_sm4.hlsl.h) | Geometry | Blend |
|------|-----------------------------------|----------|-------|
| MENU   | ribbon_sm4         | ribbon grid | DEST_COLOR / ONE |
| MENU_2 | ribbon_simple_sm4  | ribbon grid | DEST_COLOR / ONE |
| MENU_3 | simple_snow_sm4    | full quad   | SRC_ALPHA / INV_SRC_ALPHA |
| MENU_4 | snow_sm4           | full quad   | SRC_ALPHA / INV_SRC_ALPHA |
| MENU_5 | bokeh_sm4          | full quad   | SRC_ALPHA / INV_SRC_ALPHA |
| MENU_6 | snowflake_sm4      | full quad   | SRC_ALPHA / INV_SRC_ALPHA |

XMB setting `xmb_menu_color_theme` selects gradient; `menu_shader_pipeline` selects slot.

## Constant buffer (d3d11_uniform_t)
```
float4x4 modelViewProj;  // offset 0
float2   OutputSize;     // offset 64  (ribbon spells it "Outputsize")
float    time;           // offset 72
float    alpha;          // offset 76  (ribbon variants only; others stop at time)
```
Total 80 bytes (16-aligned). time increments +0.01 per pipeline draw, wraps at 65536.
NOTE: HLSL cbuffer matrices default to column-major; DirectXMath is row-major. We only
use identity mvp in Phase 1 so it is a no-op; transpose (or `row_major`) when we add
real transforms.

## Ribbon grid (menu/drivers/xmb.c)
```
XMB_RIBBON_ROWS = 64, XMB_RIBBON_COLS = 64
XMB_RIBBON_VERTICES = ROWS*(2*COLS) - 2*COLS = 8064
```
Vertex = 2 floats. Positions normalized to [-1,1]:
  x = col/(COLS-1)*2 - 1 ;  y = row/(ROWS-1)*2 - 1
Built as ONE triangle strip, serpentine per row:
  for r in 0..ROWS-2:
    for c in 0..COLS-1:
      col = (r odd) ? COLS-1-c : c
      emit vertex(r,   col)
      emit vertex(r+1, col)
Input layout: { "POSITION", R32G32_FLOAT }. Topology: TRIANGLESTRIP.

## Full quad (snow/bokeh/snowflake)
4 verts, TRIANGLESTRIP, layout { POSITION R32G32, TEXCOORD R32G32 }. VS does
mul(mvp, position); PS reads SV_POSITION.xy / OutputSize, so OutputSize must be the real
backbuffer resolution.

## Gradient color themes (xmb.c) - 4 RGBA corners each (values are /255 unless decimal)
dark_purple      20,13,20 / 20,13,20 / 92,44,92 / 148,90,148
midnight_blue    44,62,80 (all four)
apple_green      102,134,58 / 122,131,52 / 82,101,35 / 63,95,30
undersea         23,18,41 / 30,72,114 / 52,88,110 / 69,125,140
morning_blue     221,241,254 / 135,206,250 / .7,.7,.7 / 170,200,252
sunbeam          20,13,20 / 30,72,114 / .7,.7,.7 / .1,0,.1
lime_green       209,255,82 / 146,232,66 / 82,101,35 / 63,95,30
pikachu_yellow   63,63,1 / 174,174,1 / 191,194,1 / 254,221,3
gamecube_purple  40,20,91 / 160,140,211 / 107,92,177 / 84,71,132
famicom_red      255,191,171 / 119,49,28 / 148,10,36 / 206,126,110
flaming_hot      231,53,53 / 242,138,97 / 236,97,76 / 255,125,3
ice_cold         66,183,229 / 29,164,255 / 176,255,247 / 174,240,255
midgar           255,0,0 / 0,0,255 / 0,255,0 / 32,32,32
volcanic_red     1,0,.1 / 1,.1,0 / .1,0,.1 / .1,0,.1
dark             .05 (all)   ; light  .50 (all)
gray_dark        16 (all)    ; gray_light 32 (all)

## Appearance settings to port later (menu enum labels)
XMB_MENU_COLOR_THEME, XMB_THEME (icon set), XMB_LAYOUT, XMB_FONT, XMB_RIBBON_ENABLE,
XMB_SHADOWS_ENABLE, XMB_ALPHA_FACTOR, MENU_XMB_VERTICAL_FADE_FACTOR,
MENU_XMB_THUMBNAIL_SCALE_FACTOR, XMB_VERTICAL_THUMBNAILS, XMB_SWITCH_ICONS, MENU_SCALE_FACTOR.

Reference shader copies live in ./d3d_shaders/.
