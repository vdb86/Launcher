# RetroArch XMB layout + animation constants (for Phase 2 port)

Source: libretro/RetroArch (master), GPLv3. Extracted from `menu/drivers/xmb.c` and
`gfx/gfx_animation.{c,h}`. These are the real numbers XMB uses; port them 1:1 so the
menu layout, selection zoom, and transitions feel exactly like XMB.

## Scale factor (PS3 layout is our target)
`xmb_get_scale_factor` for the PS3 layout:
```
scale_factor = (menu_scale_factor * width) / 1920.0f      // width = backbuffer width in px
```
`menu_scale_factor` is a user setting (default 1.0). So at 1920 wide, scale_factor = 1.0.
`scale_cap = (menu_scale_factor > 1) ? menu_scale_factor : 1`  (so normally scale_cap = 1).

Everything below is the PS3 layout (`xmb_layout_ps3` + `xmb_layout_common`), which is the
classic full-size XMB look. PSP layout is the small-screen variant - ignore for now.

## Row-position offsets (vertical list geometry), PS3
```
above_subitem_offset =  1.5 / scale_cap     (min 1.5)
above_item_offset    = -1.0 / scale_cap
active_item_factor   =  3.0 / scale_cap      (min 2.0)
under_item_offset     =  5.0 / scale_cap      (min 3.0)
```
`xmb_item_y(i, current)` returns the y offset of item i relative to the selected item
`current`, in units of `icon_spacing_vertical`:
- i < current:            spacing * (i - current + above_item_offset)
- i == current:           spacing * active_item_factor
- i > current:            spacing * (i - current + under_item_offset)
(There's a sub-item branch for expanded entries; ignore until we support sub-items.)

## Sizes / spacing (PS3), all multiplied by scale_factor unless noted
```
font_size             = 32 * scale_factor            (min 7)
font2_size            = 22 * scale_factor            (sublabels; min 6)
icon_size             = 128 * scale_factor           (then /= scale_cap)
cursor_size           = 64 * scale_factor
icon_spacing_horizontal = 192 * scale_factor / scale_cap   (gap between category icons)
icon_spacing_vertical   = 64  * scale_factor               (gap between list rows)
margins_screen_top     = (256 + 16) * scale_factor / scale_cap
margins_screen_left    = 336 * scale_factor / scale_cap
margins_label_left     = 85  * scale_factor / scale_cap    (list text x from icon)
margins_label_top      = font_size / 3.0
margins_title_left     = margins_title*scale + 4*scale + h_offset*scale
margins_title_top      = margins_title*scale + (font_size - (font_size/6)*scale)
shadow_offset          = clamp(4 * scale_factor, 1.0, 2.0)
```
Note: at 1920x1080 with menu_scale_factor=1, scale_factor=1 so these are the raw px values
(icon 128, row spacing 64, category spacing 192, top margin 272, left margin 336, font 32).

## Selection zoom + alpha (xmb_layout_common)
```
categories_active_zoom  = 1.0     categories_passive_zoom  = 0.5
items_active_zoom       = 1.0     items_passive_zoom       = 0.5
categories_active_alpha = 1.0     categories_passive_alpha = 0.75
items_active_alpha      = 1.0     items_passive_alpha      = 0.75
```
The selected category/item draws at zoom 1.0 alpha 1.0; unselected shrink to 0.5 zoom and
fade to 0.75 alpha. Icons interpolate toward these targets via animation (below).

## Animation (gfx_animation)
```
XMB_DELAY        = 166.66667   (ms; standard tween duration for XMB moves)
XMB_EASING_ALPHA = EASING_OUT_CIRC   (alpha/zoom fades)
XMB_EASING_XY    = EASING_OUT_QUAD   (x/y position slides)
```
Easing signatures are `f(t, b, c, d)`: t=current time, b=begin value, c=change (end-begin),
d=duration. Return the eased value.
```
easing_out_quad(t,b,c,d):  t = t/d;              return -c * t * (t - 2) + b;
easing_out_circ(t,b,c,d):  base = t/d - 1;       return c * sqrt(1 - base*base) + b;
```
Our animation clock already runs in RA-equivalent units (see MEMORY: gTime += dt*0.6 where
0.6 == 0.01*60). For tweens we need a per-property timer in ms; drive it off the same QPC dt
(dt seconds * 1000 = ms elapsed) and clamp t to [0,d].

## Ribbon geometry / cbuffer
Already ported in Phase 1 - see reference/retroarch/NOTES.md.

## Exact draw positions (from xmb.c render, verbatim - stop guessing these)
All coordinates in XMB space (y from top). `half_size = icon_size/2`. `node->y = xmb_item_y(i, current)`.

Vertical list (per entry):
- item ICON top-left: x = node->x + margins_screen_left + icon_spacing_horizontal - half_size
                      y = margins_screen_top + node->y + half_size          (xmb.c ~5808/5816)
- item LABEL: x = node->x + margins_screen_left + icon_spacing_horizontal
                 - (show_entry_icons ? 0 : icon_size) + margins_label_left   (~6073)
              y = margins_screen_top + node->y + label_offset  (label_offset = margins_label_top;
                  flips to -margins_label_top for the active item when a sublabel is shown)
  => with entry icons ON: label x = margins_screen_left + icon_spacing_horizontal + margins_label_left.
     Item icon COLUMN centre and the ACTIVE category icon share the same x (margins_screen_left +
     icon_spacing_horizontal). At 1920x1080 scale 1: icon col centre x=528, label x=613.

Horizontal category tabs (icons only; the selected category NAME is the title, not a per-icon label):
- x = xmb->x + categories_x_pos + margins_screen_left + icon_spacing_horizontal*(i+1) - icon_size/2
     where categories_x_pos = icon_spacing_horizontal * -selected_category  (~9340)
     => centre x = margins_screen_left + icon_spacing_horizontal*(i+1 - selected); active => +192.
- y = margins_screen_top + icon_size/2                                       (tab row centre)
- scale = node->zoom (active 1.0 / passive 0.5)

Title (active category name): x = margins_title_left (+icon_len), y = margins_title_top  (~10011).
  margins_title = menu_xmb_title_margin*10 (default small); title_top = margins_title*scale +
  (font_size - (font_size/6)*scale).

xmb_draw_icon: draw.x = x, draw.y = height - y (RA uses bottom-left origin; y is flipped). Icon draws
from that anchor with width/height = icon_size*scale.

## xmb_item_y (verbatim, PS3 top-level)
```
if (i <  current) return spacingV * ((i-current) + above_item_offset);   // above_item_offset -1.0
if (i == current) return spacingV *  active_item_factor;                 // 3.0
                  return spacingV * ((i-current) + under_item_offset);   // under_item_offset 5.0
```
So (scale 1, spacingV 64): active item at +192 from margins_screen_top; first item below at +384
(a deliberate ~3-row gap under the selection); items above start at -128. RA animates each node->y
toward these targets on selection change (per-node tween), easing XMB_EASING_XY = out_quad.

## Menu-action mapping (to port in step 3)
XMB consumes generic MENU_ACTION_UP/DOWN/LEFT/RIGHT/OK/CANCEL from menu_event. For our host:
- Up/Down     -> move selection within the current category's item list (vertical).
- Left/Right  -> move between categories (horizontal), reset item selection appropriately.
- OK (A / Enter)     -> enter category / launch app.
- Cancel (B / Esc)   -> back up a level / (at top) do nothing or quit per config.
We map XInput + Raw Input keyboard to these actions, then feed one action enum into the
menu logic (mirrors RA's menu_event -> menu_entry_action path).
