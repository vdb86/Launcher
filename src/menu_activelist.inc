// --- active list abstraction ---------------------------------------------------------------------
// The active category's item list, exposed as count / selected index / per-entry title + icon so
// RetargetItems, the fixed-scroll math and DrawMenu's item loop share one path. (Settings live in a
// popup, not the menu, so the active list is always a real category's items.)
static int ActiveCount()
{
    if (gCatSel >= 0 && gCatSel < (int)gMenu.size() && gMenu[gCatSel].isItem) return 0; // top-level item: no sub-menu
    int n = (gCatSel < (int)gMenu.size()) ? (int)gMenu[gCatSel].items.size() : 0;
    return gEditMode ? n + 1 : n;        // edit mode: one extra slot at the end = the "+" (Add item)
}
static int ActiveSel()
{
    return (gCatSel < (int)gItemSel.size()) ? gItemSel[gCatSel] : 0;
}
// Fill an entry (label, value shown on the right - always empty for items, icon glyph name) for
// index i of the active list.
static void ActiveEntry(int i, std::wstring& title, std::wstring& value, std::wstring& icon)
{
    title.clear(); value.clear(); icon.clear();
    int n = (gCatSel < (int)gMenu.size()) ? (int)gMenu[gCatSel].items.size() : 0;
    if (i >= 0 && i < n) { const MenuItem& it = gMenu[gCatSel].items[i]; title = it.title; icon = it.icon; }
    else if (gEditMode && i == n) { title = L"Add item"; icon = L"menu_add"; }   // the "+" add slot
}

// How many item / category slots fit on screen in fixed placement (used to decide when the fixed
// list must scroll to keep the selector visible). Mirrors the fixed-layout origins in DrawMenu.
static int VisibleItemSlots(const MenuLayout& L)
{
    float avail = gVertical
        ? ((float)gW - (48.0f * L.scale + L.iconSize * 2.0f) - 40.0f * L.scale)   // strip to the right
        : ((float)gH - (L.marginTop + L.iconSize * 1.2f)     - 40.0f * L.scale);  // column below the row
    float step = gVertical ? L.itemPitchV : FixedColPitchV(L);
    int v = (step > 1.0f) ? (int)(avail / step) : 1;
    return v < 1 ? 1 : v;
}
static int VisibleCatSlots(const MenuLayout& L)
{
    // The category run can use (nearly) the whole axis - only a small pad each end is reserved. (The old
    // formula reserved a big start margin, so a handful of categories read as "overflowing" and the
    // alignment anchor fell back to the scroll anchor - which is why Vertical align did nothing.)
    float avail = (gVertical ? (float)gH : (float)gW) - 80.0f * L.scale;          // full axis minus pad
    int v = (L.catPitch > 1.0f) ? (int)(avail / L.catPitch) : 1;
    return v < 1 ? 1 : v;
}
// In fixed placement keep the scroll offset stable until the selection would leave the window,
// then move it by the minimum. No-op (offsets 0) when everything fits or when not in fixed mode.
static void UpdateFixedScroll()
{
    if (!gFixedLayout || gMenu.empty()) { gFixedItemScroll = 0; gFixedCatScroll = 0; return; }
    MenuLayout L = ComputeLayout(gW, gH);
    int n = ActiveCount(), sel = ActiveSel(), vis = VisibleItemSlots(L);
    if (n <= vis) gFixedItemScroll = 0;
    else {
        if (sel < gFixedItemScroll)             gFixedItemScroll = sel;
        if (sel > gFixedItemScroll + vis - 1)   gFixedItemScroll = sel - vis + 1;
        if (gFixedItemScroll < 0)               gFixedItemScroll = 0;
        if (gFixedItemScroll > n - vis)         gFixedItemScroll = n - vis;
    }
    int cn = (int)gMenu.size(), cvis = VisibleCatSlots(L);
    if (cn <= cvis) gFixedCatScroll = 0;
    else {
        if (gCatSel < gFixedCatScroll)            gFixedCatScroll = gCatSel;
        if (gCatSel > gFixedCatScroll + cvis - 1) gFixedCatScroll = gCatSel - cvis + 1;
        if (gFixedCatScroll < 0)                  gFixedCatScroll = 0;
        if (gFixedCatScroll > cn - cvis)          gFixedCatScroll = cn - cvis;
    }
}

// Aim every item's y/selectedness at the targets for the current selection. snap = jump (category
// switch / first build); otherwise items slide over XMB_DELAY with out_quad easing. snapCats does the
// same for the fixed-placement category tweens (false = glide, used on a category change so the
// highlight + item block glide to the new category while the item slots snap into the new list).
static void RetargetItems(bool snap, bool snapCats = true)
{
    if (gMenu.empty()) return;
    MenuLayout L = ComputeLayout(gW, gH);
    int n   = ActiveCount();      // real category items, or settings sections / section rows
    int sel = ActiveSel();
    if ((int)gItemYT.size() != n)
    {
        gItemYT.assign(n, Tween());
        gItemZT.assign(n, Tween());
        gItemAT.assign(n, Tween());
        gItemXT.assign(n, Tween());                                 // node->x parallax (out_quad default)
        for (int i = 0; i < n; ++i) gItemAT[i].easeFn = EaseOutCirc; // XMB_EASING_ALPHA
        snap = true;
    }
    for (int i = 0; i < n; ++i)
    {
        // Along-list offset. Horizontal = the splayed XMB column (active item gapped from its
        // neighbours). Vertical = an even horizontal strip (spacingH apart) that scrolls so the
        // selected item stays at a fixed slot - the splayed gap reads oddly transposed to X.
        float ty;
        if (gVertical) {                              // even strip; focusGap adds a gap around the active item
            float d = (float)(i - sel), ex = L.itemPitchV * (CurTune().focusGap - 1.0f);
            ty = L.itemPitchV * d + (d > 0 ? ex : d < 0 ? -ex : 0.0f);
        } else ty = XmbItemY(L, i, sel);
        float ts = (i == sel) ? 1.0f : 0.0f;
        if (snap) { gItemYT[i].set(ty); gItemZT[i].set(ts); gItemAT[i].set(ts); }
        else      { gItemYT[i].retarget(ty, XMB_DELAY); gItemZT[i].retarget(ts, XMB_DELAY); gItemAT[i].retarget(ts, XMB_DELAY); }
    }
    UpdateFixedScroll();        // keep the fixed-placement selector on screen (no-op otherwise)

    // Fixed-placement slide tweens: aim the list scroll + selection at their new (integer) targets so
    // the list glides one slot on a scroll and the highlight glides between elements (see decls).
    float itScroll = (float)gFixedItemScroll;
    float itSel    = (float)ActiveSel();
    if (snap) { gFixItemScrollT.set(itScroll); gFixItemSelT.set(itSel); }
    else      { gFixItemScrollT.retarget(itScroll, XMB_DELAY); gFixItemSelT.retarget(itSel, XMB_DELAY); }
    float caScroll = (float)gFixedCatScroll, caSel = (float)gCatSel;
    if (snapCats) { gFixCatScrollT.set(caScroll); gFixCatSelT.set(caSel); }
    else          { gFixCatScrollT.retarget(caScroll, XMB_DELAY); gFixCatSelT.retarget(caSel, XMB_DELAY); }
}

// Horizontal parallax on a category switch (ported xmb_list_switch_new / xmb_push_animations,
// xmb.c ~2727): the incoming list starts one icon_spacing_horizontal along the category axis in
// the travel direction (node->x = icon_spacing_horizontal * dir) and slides to 0 (out_quad), while
// the whole list fades 0 -> 1 (out_circ = XMB_EASING_ALPHA). Both over XMB_DELAY. Call AFTER
// RetargetItems(true) so the item vector is already sized for the new category.
static void StartListSwitch(int dir)
{
    MenuLayout L = ComputeLayout(gW, gH);
    // The incoming list's motion is the user's "List animation" choice (CurTune().listAnim, per-mode):
    //   0 None       - appear in place, no slide, no fade.
    //   1 Slide      - original XMB parallax ACROSS the menu bar (X offset), + fade. (default)
    //   2 Fade       - fade only, no positional slide.
    //   3 Axis slide - slide ALONG the item-list axis (a horizontal menu's vertical column slides in Y;
    //                  a vertical menu's horizontal strip slides in X), + fade.
    // gListSlideAxis tells DrawMenu which axis the gItemXT offset applies to (0 = X, 1 = Y).
    float off = 0.0f;
    gListSlideAxis = 0;
    const int anim = CurTune().listAnim;   // per-mode (orientation x placement)
    switch (anim)
    {
    case 1:  // Slide = classic XMB list-switch parallax ALONG THE CATEGORY (travel) axis: X for a horizontal
             // menu (left/right), Y for a vertical menu (up/down). Direction follows the nav direction (dir).
             // Fixed placement rides the selector glide instead, so it adds no extra offset there.
             if (gFixedLayout)   off = 0.0f;
             else if (gVertical) { gListSlideAxis = 1; off = L.spacingH * (float)dir; }  // vertical: category axis = Y
             else                { gListSlideAxis = 0; off = L.spacingH * (float)dir; }  // horizontal: category axis = X
             break;
    case 3:  // Axis slide: ALWAYS from the menu bar toward the submenu (a horizontal menu's column slides
             // DOWN, a vertical menu's strip slides RIGHT), independent of the nav direction. Computed in the
             // forward frame with a negative offset (start on the bar side, settle outward); reflectPt in
             // DrawMenu mirrors it for a reversed submenu (up / left) automatically.
             if (gVertical) { gListSlideAxis = 0; off = -L.itemPitchV; }                  // strip  -> X (toward right)
             else           { gListSlideAxis = 1; off = -L.spacingV;   }                  // column -> Y (toward below)
             break;
    default: off = 0.0f; break;                                                          // None / Fade
    }
    for (Tween& t : gItemXT) { t.easeFn = EaseOutQuad; t.set(off); t.retarget(0.0f, XMB_DELAY); }
    if (anim == 0) { gListFade.set(1.0f); }                      // None: no fade, list just appears
    else if (anim == 2) { gListFade.easeFn = EaseLinear;  gListFade.set(0.0f); gListFade.retarget(1.0f, kListFadeMs); } // Fade: gentle, longer standalone fade
    else { gListFade.easeFn = EaseOutCirc; gListFade.set(0.0f); gListFade.retarget(1.0f, XMB_DELAY); }   // Slide / Axis slide: quick fade riding the motion
}

// Fixed placement, for None / Fade / Axis slide: the category selector GLIDES to the new column first
// (the item list stays hidden), and only when it settles does the chosen animation play - at the settled
// column (user request). Slide is exempt (it glides along with the selector). MoveCategory arms this; the
// main loop ticks it each frame.
static void UpdateListSwitch(float dtMs)
{
    if (gListSwitchDelayMs < 0.0f) return;
    gListSwitchDelayMs -= dtMs;
    if (gListSwitchDelayMs <= 0.0f) { gListSwitchDelayMs = -1.0f; StartListSwitch(gListSwitchDir); }
}

static void CreateMenuFormats();   // fwd: title/label sizes are per-layout, so recreate on a mode switch

// Toggle horizontal/vertical orientation and re-snap item positions (the along-list targets differ
// per orientation - see RetargetItems). Title sizes are per (orientation x placement), so recreate the
// text formats too.
static void SetVertical(bool v)
{
    if (gVertical == v) return;
    gVertical = v;
    CreateMenuFormats();
    RetargetItems(true);
}

// Toggle classic-scrolling vs fixed placement and re-snap positions. Temp entry point ('L') until
// the settings UI exists.
static void SetPlacement(bool fixed)
{
    if (gFixedLayout == fixed) return;
    gFixedLayout = fixed;
    gCatTween.set((float)gCatSel);
    CreateMenuFormats();
    RetargetItems(true);
}

// DirectWrite formats sized from layout; recreated on resize.
static IDWriteTextFormat* gItemFmt = nullptr;
static IDWriteTextFormat* gCatFmt  = nullptr;
static IDWriteTextFormat* gItemTitleFmt = nullptr;   // real menu-item labels (size = current layout's itemTitleScale)
static IDWriteTextFormat* gCatTitleFmt  = nullptr;   // active-category title (size = current layout's catTitleScale)
static IDWriteTextFormat* gClockTimeFmt = nullptr;   // on-screen clock: time line (larger)
static IDWriteTextFormat* gClockDateFmt = nullptr;   // on-screen clock: date line (smaller)
static IDWriteTextFormat* gSubFmt       = nullptr;   // small font: settings row descriptions + hints

// Clock placement = one of the four screen corners. The always-on menu clock sits top-right; the
// screensaver clock hops between corners (see below). There is no centre position.
enum ClockPos { CLOCK_TL, CLOCK_TR, CLOCK_BR, CLOCK_BL };
// Screensaver clock: when the screensaver is up it appears in a corner and moves to a NEW corner
// every minute. Motion picks the next corner: 0 = clockwise, 1 = counterclockwise, 2 = random.
// Config-persisted ("screensaver.clock"); temp key 'C' cycles off / CW / CCW / random for now.
static bool gSaverClockEnabled = true;
static int  gSaverClockMotion  = 0;              // 0 clockwise / 1 counterclockwise / 2 random
static int  gSaverCorner       = 1;              // current corner as an index into kCwCorners (TR)
static int  gClockLastMin      = -1;             // last minute drawn (to detect a minute change)
// Clock size multiplier (both the menu clock and the screensaver corner clock scale by this).
// Config-persisted ("menu.clockScale").
static float gClockScale = 1.0f;
// Clock / date appearance options (config-persisted under "clock").
static bool gClockBold   = false;   // draw the time line bold
static bool gDateBold    = false;   // draw the date line bold
static bool gDateEnabled = true;    // show the date line under the time
static bool gBlinkColon  = true;    // blink the time colon (1s visible, 1s not)
static int  gHourFormat  = 0;       // 0 = follow Windows, 1 = 12-hour (localized), 2 = 24-hour, 3 = 12-hour (English)
static int  gDateFormat  = 4;       // index into the date-format list (see DrawClock / SettingValue S_DATEFMT); 13 formats (0-12)
static int  gDateSep      = 0;      // numeric-date separator: 0 = "." / 1 = "-" / 2 = "/"
static int  gClockCorner  = CLOCK_TR; // menu-clock corner (CLOCK_TL/TR/BR/BL)
// Whole-UI font (categories, items, clock, settings). Config-persisted ("menu.font"). Curated list
// of common Windows fonts; the setting cycles gUiFontIdx through kUiFonts.
static const wchar_t* kUiFonts[] = { L"Segoe UI", L"Arial", L"Verdana", L"Tahoma",
                                     L"Calibri", L"Georgia", L"Consolas", L"Times New Roman" };
static const int kUiFontCount = (int)(sizeof(kUiFonts) / sizeof(kUiFonts[0]));
static int gUiFontIdx = 0;          // index into kUiFonts
static const wchar_t* UiFont() { return kUiFonts[(gUiFontIdx >= 0 && gUiFontIdx < kUiFontCount) ? gUiFontIdx : 0]; }
// Fixed-placement only: whether the selected element enlarges (icon + label zoom) or stays a uniform
// size with just the outline marking the selection. Config-persisted ("menu.enlargeSelected").
static bool gEnlargeSelected = true;
// Corners in clockwise order; gSaverCorner indexes this. Counterclockwise steps by +3 (== -1).
static const ClockPos kCwCorners[4] = { CLOCK_TL, CLOCK_TR, CLOCK_BR, CLOCK_BL };
