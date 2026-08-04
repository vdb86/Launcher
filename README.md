# Launcher (working title)

Fullscreen Windows 10/11 HTPC application launcher with the look and feel of RetroArch's
**XMB** interface. Controller / keyboard / remote first - no mouse required.

It ports RetroArch's XMB rendering (the animated ribbon/snow/bokeh background pipelines,
the menu layout, easing and transitions) into a slim native C++/Direct3D 11 host, and
replaces RetroArch's data model with a simple JSON category/app configuration. Because it
reuses RetroArch code, the project is licensed under **GPLv3** (see `LICENSE`).

## Features

- Two-level menu (Category then Item); a top-level node can itself be a launchable app.
- Launches apps and regains focus when they exit (Job Objects track the process tree;
  optional hide-while-running and window-tracking launch modes).
- Six XMB background pipelines (Ribbon, Simple Ribbon, Simple Snow, Snow, Bokeh, Snowflake)
  over selectable colour themes, custom RGB / gradient, a full-screen image, or a slow
  ribbon colour cycle. The ribbon animates continuously.
- Idle screensaver: the menu fades out to the background-only animation with a corner clock;
  any input restores it.
- In-engine, controller-first edit mode: on-screen keyboard, file + image icon pickers, and
  a move/reorder mode - no native mouse dialogs.
- Extensive layout options (orientation H/V, scrolling/fixed placement, spacing, sizes,
  alignment, menu scale/position), a configurable clock & date, and screensaver settings.
- Windows integration: autostart (HKCU Run), single-instance, cold-boot foreground, and an
  optional shell-replacement kiosk mode with backup/restore and a recovery path.

## Input

Keyboard arrows / Enter / Esc / Backspace, an XInput gamepad D-pad + A/B/Back/Start, and
injected input from remotes all drive the menu: a phone web-remote (arrow keys via
`keybd_event`) and an HDMI-CEC helper that maps TV-remote buttons to arrow keys both work.
In the plain menu a short OK press launches and a long press opens Settings, so a D-pad + OK
remote is enough.

## Build

Requires Visual Studio 2019/2022 ("Desktop development with C++": MSVC + Windows SDK) and
CMake 3.20+ (the copy bundled with Visual Studio is auto-located - it need not be on PATH).

```
build.bat
```

Produces `build\Release\launcher.exe`. Shaders are compiled at runtime via `D3DCompile`, so
there is no offline shader step. The MSVC runtime is static-linked, so `launcher.exe` is a
portable xcopy deploy (no VC++ redistributable needed).

## Configuration

`launcher.json` is read only from the exe's own folder. On a fresh install the app starts
with one empty "New" category and drops into edit mode so you can add your apps on-device;
autostart and kiosk on/off are read live from the registry, not stored in the JSON.

## Layout

```
src/            application source (single translation unit: main.cpp + .inc section files)
  main.cpp        Win32 + D3D11 host, render loop, input, window management
  menu_*.inc      data model, active list, icons, drawing
  config_io.inc   launcher.json load/save
  launch.inc      app launching (job object, hide/wait/focus-regain)
  input_actions.inc  input -> menu actions
  edit_mode.inc   in-engine editor (keyboard, file/image pickers, move mode)
  settings.inc    Settings screens + colour picker
  shaders.h       the 6 background pipeline shaders (ported) + gradient shader
  themes.h        XMB gradient colour themes
  backgrounds.h / json.h   background registry + minimal JSON parser
reference/        RetroArch reference material (ported shaders + porting notes)
docs/             supplementary docs (BACKGROUNDS.md)
PLANS.md          roadmap / status
MEMORY.md         decisions, context, gotchas
```

## Credits & license

Ribbon backgrounds and XMB menu logic are derived from
[RetroArch](https://github.com/libretro/RetroArch) (GPLv3). This project is therefore
distributed under the GNU General Public License v3.0 - see `LICENSE`.

Two backgrounds are ports of shaders from [Shadertoy](https://www.shadertoy.com):

- **Synthwave Road** by *alexdav* - https://www.shadertoy.com/view/7ltcRn
- **Orbs** by *xephosbot* - https://www.shadertoy.com/view/cd3yRs

See `docs/BACKGROUNDS.md` for the full attribution table. Shadertoy shaders are CC BY-NC-SA 3.0 by
default unless the author states otherwise; verify each shader's stated license before commercial
redistribution.
