# Background pipelines

Background and ribbon are selected INDEPENDENTLY and composited each frame as
gradient -> background -> ribbon (the ribbon brightens whatever is beneath it). Either can be set
to "None". The two ribbons (1-2) are ribbon overlays; everything else (RetroArch snow/bokeh/
snowflake + the originals) are backgrounds.

Controls:
- Background: Up/Down (keyboard), D-pad Up/Down (gamepad). Number keys 1-9 pick a background by
  index; 0 = background None.
- Ribbon: `[` / `]` (keyboard), LB/RB shoulders (gamepad). Cycles ribbons + None.
- Theme: Left/Right (keyboard), D-pad Left/Right (gamepad).
- Esc / gamepad B: quit.
On-screen label: `Bg: <name>    Ribbon: <name>    Theme: <name>`. Default at startup is
ribbon on + background None (classic XMB look).

Pipelines 1-6 are ported from RetroArch (GPLv3). Pipelines 7-12 are original / ported
implementations for this project (technique-inspired, not copied). All render over the gradient
color theme; "opaque" effects cover it, "over gradient" effects let it show through.

Constant inputs available to every effect: `global.time` (seconds, delta-timed) and
`global.OutputSize` (pixels). New effects are fullscreen-quad pixel shaders in `src/backgrounds.h`.

## RetroArch backgrounds (ported)
| Name | Looks like | Technique | Status |
|------|-----------|-----------|--------|
| Simple Snow | Light, sparse falling snow. | Fullscreen, layered dots. | ok (fall fixed) |
| Snow | Denser, larger falling snow. | Fullscreen, layered dots. | ok (fall fixed) |
| Bokeh | Soft drifting colored light blobs. | Fullscreen loop. | ok |

(Ribbon and Simple Ribbon are ported from RetroArch too, but they are *ribbons* - see below.)

## Original / ported backgrounds
| Name | Looks like | Technique | Status |
|------|-----------|-----------|--------|
| Caustics | Rippling light like sunlight on a pool floor. | Wave interference. | untested |
| Synthwave Road | Sunset, distant skyline, bending neon road receding to horizon. | Port of Shadertoy 7ltcRn (no loops, cheap). | untested |
| Orbs | Two soft glowing orbs (cyan + magenta) slowly orbiting on black. | Port of Shadertoy cd3yRs; distance + smootherstep (no loops, very cheap). | untested |

All current backgrounds are light (loop-free or fullscreen loops).

## Attribution (Shadertoy sources)
Two backgrounds are ports of shaders published on [Shadertoy](https://www.shadertoy.com). Shadertoy
content is, by default, licensed under CC BY-NC-SA 3.0 unless the author states otherwise; ports
below keep the credit and link back to the original.

| Background | Shadertoy | URL | Author |
|------------|-----------|-----|--------|
| Synthwave Road | 7ltcRn | https://www.shadertoy.com/view/7ltcRn | alexdav |
| Orbs | cd3yRs | https://www.shadertoy.com/view/cd3yRs | xephosbot |

Note: verify each shader's stated license on its page; if either is non-commercial (CC BY-NC-SA),
that constrains redistribution of that background.

## Ribbons
Ribbons are overlays selected separately (see controls above):
- Ribbon, Simple Ribbon - ported from RetroArch; DEST_COLOR blend, so they *multiply/brighten*
  whatever is beneath (gradient or a background). Their color comes from what's underneath, so on
  a pure-black theme they are invisible.
- Custom Colour Ribbon (ours) - same wavy geometry, ALPHA blend (translucent), so it shows on
  pure black without blowing out. Colour + opacity come from a 2nd constant buffer (b1), set from
  C++ globals `gRibbonColor` / `gRibbonOpacity` (a settings UI will drive these later; defaults =
  gold, opacity 1). Opacity curve: alpha = saturate(sheen * opacity).

## Color themes
Left/Right cycles gradient themes (`src/themes.h`). This is the full RetroArch XMB set: 21 themes,
1:1 with upstream `xmb.c` (Legacy Red, Dark Purple, Midnight Blue, Golden, Electric Blue, Apple
Green, Undersea, Volcanic Red, Dark, Light, Morning Blue, Sunbeam, Lime Green, Midgar, Pikachu
Yellow, GameCube Purple, Famicom Red, Flaming Hot, Ice Cold, Gray Dark, Gray Light). No custom
themes - the Custom Colour Ribbon carries its own colour, so no dedicated black theme is needed.
Startup default = "Dark" (RetroArch's near-black) + Custom Colour Ribbon + background None.

Corner order matches RetroArch exactly: each theme stores 4 RGBA corners as c[0]=BL c[1]=BR
c[2]=TL c[3]=TR (RA's "bottom-up: BL BR TL TR" vertex order). UpdateGradientVB maps them so the
gradient is oriented like RetroArch (this fixed an earlier vertical flip).

Removed backgrounds: Synthwave Grid, Low-Poly Drift, Snowflake (2026-07-28), plus earlier Clouds
(fast-normal pixelation), Aurora (too heavy), Drifting Blocks + original Sparks (choppy), Stars,
Starfield, Lava Lamp, Embers, and the first cull of Liquid Silk, Constellation, Nebula, Living
Gradient, Rain, Kaleidoscope, Warp Tunnel, Ripple Pond, Ink Plumes, God Rays, Ocean, Falling
Petals, Meteor Shower, Plasma Arcs, Neon Terrain, Matrix Rain.

## Adding a new background later
1. Add a `kBgXxx` shader string to `src/backgrounds.h` (PSMain over the quad).
2. Add one `addBg("Name", kBgXxx);` line in `InitPipelines()` in `src/main.cpp`.
3. Add a row here. That's it - it joins the Up/Down cycle automatically.
