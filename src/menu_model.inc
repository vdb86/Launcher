// --- data model (Phase 3: populated from the JSON config; see LoadConfig) ---
// An item is normally a launchable entry (has `target`). Fields mirror the schema in MEMORY.md.
struct MenuItem
{
    std::wstring type;            // "app" (default) - reserved: "category" for future submenus
    std::wstring title;
    std::wstring icon;            // glyph name (assets/tabler/png/<icon>.png) or an image/file path
    std::wstring target;          // path/command/URL/shortcut to launch (empty = non-launchable placeholder)
    std::wstring arguments;
    bool waitForExit       = true;   // track the process + regain focus when it exits (false = fire-and-forget)
    bool trackWindow       = false;  // window-watch mode: for single-instance / tray apps that hand off to an
                                     // already-running instance (so the spawned process exits at once). Instead
                                     // of waiting on the process, watch the app's window and return to the
                                     // launcher when it CLOSES or HIDES to the tray/taskbar.
    std::wstring processName;        // optional image name to watch (e.g. "CouchTube.exe"); empty = derive from target
};
struct MenuCategory { std::wstring title; std::wstring icon; std::vector<MenuItem> items;
                      std::wstring target; std::wstring arguments;   // used only when isItem (direct-launch tab)
                      bool waitForExit = true;  // (isItem only) track the process + regain focus on exit; false = fire-and-forget
                      bool trackWindow = false; // (isItem only) window-watch launch mode (see MenuItem.trackWindow)
                      std::wstring processName; // (isItem only) optional image name to watch; empty = derive from target
                      bool isItem = false; };   // true = a top-level launcher tab with NO sub-menu (OK launches target)
static std::vector<MenuCategory> gMenu;
static int              gCatSel = 0;          // active category index
static std::vector<int> gItemSel;             // active item index, per category
// Part C: whole-menu screen offset (moves the category row/column + item list + titles together). Set
// via the "Menu position..." live nudge mode; persisted in the "menu" config block. 0,0 = default.
static float gMenuPosX = 0.0f, gMenuPosY = 0.0f;
static bool  gPosMode  = false;               // true while nudging the menu position
static float gPosOrigX = 0.0f, gPosOrigY = 0.0f;  // saved offset to restore on cancel

// Settings model (RetroArch-style rows, grouped under section headers). The enum + selection live
// here so the menu draw (DrawMenu) and input (ApplyMenuAction) can drive the Settings tab directly;
// the Setting* function bodies are defined further down.
enum SettingRow {
    SH_APPEAR, S_BG, S_BGIMAGE, S_RIBBON, S_RIBBONCOL, S_RIBBONCYCLE, S_RIBBONOPACITY, S_RIBBONSHEEN,
    S_THEME, S_THEMETOPCOL, S_THEMEBOTCOL,
    S_ORIENT, S_PLACE, S_ENLARGE, S_UIFONT,
    S_CLOCKSIZE, S_CLOCKCORNER, S_CLOCKBOLD, S_DATEON, S_DATEBOLD, S_DATEFMT, S_DATESEP, S_HOURFMT, S_BLINKCOLON,
    SH_SAVER, S_SAVER, S_TIMEOUT, S_DIMEN, S_DIM, S_SAVERCLOCK,
    SH_SYSTEM, S_KEEPAWAKE, S_RETURNHOME, S_AUTOSTART, S_KIOSK, SH_MENU,
    // menu-layout tuning (batch 10): 4 spacing knobs + 4 title controls
    S_ALIGNH, S_ALIGNV, S_SUBDIR, S_LISTANIM, S_MENUSCALE,
    S_SP_ITEM, S_FOCUSGAP, S_SP_CATITEM, S_SP_CAT, S_SP_ICONTITLE,
    S_ITEMTITLE_SIZE, S_ITEMTITLE_SHOW, S_CATTITLE_SIZE, S_CATTITLE_SHOW, S_CATTITLE_ALL, S_CAT_ICONTITLE,
    S_CATICON_SIZE, S_ITEMICON_SIZE,
    S_MENUPOS, S_MENUPOSRESET,
    S_EDIT, S_CLOSE, S_COUNT
};
static int gSettingsSel = S_BG;
// RetroArch-style Settings SUBMENUS (XMB depth): the Settings tab root lists SECTIONS; OK on a section
// pushes that section's rows as a submenu (the vertical list slides across + a new title shows). Back
// pops. gSettingsDepth 0 = section list, 1 = inside a section; gSettingsSection = the open section;
// gSectionSel = the highlighted section at depth 0; gSettingsSel = the highlighted row at depth 1.
enum SettingsSection { SEC_APPEAR, SEC_CLOCK, SEC_SAVER, SEC_SYSTEM, SEC_MENU, SEC_EDIT, SEC_COUNT };
static int gSettingsDepth   = 0;          // 0 section list / 1 section rows / 2 option picker
static int gSettingsPickSnap = 0;         // option index to restore if the picker is cancelled
static int gSettingsSection = SEC_APPEAR;
static int gSectionSel      = 0;
static const wchar_t* SectionLabel(int s);
static const wchar_t* SectionDesc(int s);
static const int*     SectionRows(int s, int* count);   // the SettingRow list for a section
static int            SectionFirstRow(int s);            // first non-hidden row of a section
static int            SectionStep(int dir);              // move gSettingsSel within the active section
static const wchar_t* SettingLabel(int r);
static std::wstring   SettingValue(int r);
static const wchar_t* SettingDesc(int r);
static void           SettingApply(int r, int delta);
static void           EnterEditMode();          // drop straight into live edit mode (from the Edit section)
static bool           SettingIsHidden(int r);   // Ribbon/Theme rows hidden unless Background = Ribbon
// UI state machine (declared here, before DrawMenu, so drawing can read it). UI_BROWSE = normal menu;
// UI_SETTINGS = settings screen; UI_ACTION = edit context popup; UI_KEYBOARD / UI_FILES = field entry.
enum UiMode { UI_BROWSE, UI_SETTINGS, UI_ACTION, UI_KEYBOARD, UI_FILES, UI_COLOR };
static UiMode gUi = UI_BROWSE;
// Two-press exit (RetroArch quit_press_twice): the first quit press arms this timer (ms remaining) and
// shows an on-screen "press again to exit" prompt; a second quit press while it is >0 actually exits.
static float  gQuitPromptTimer = 0.0f;
// Startup hint: on launch, briefly show a banner telling the user how to open Settings (fades out over
// its final second). Cleared early once Settings is opened. TV-remote friendly: OK can be long-pressed.
static float  gSettingsHintMs = 9000.0f;
static const float kOkLongPressMs = 600.0f;   // hold OK (Enter / gamepad A) this long in browse -> open Settings
// Edit mode: entered from Settings. While on, the user navigates the REAL menu (so the item being
// edited is always visible + selectable); OK opens a context popup for that item instead of launching,
// and B/Esc/Back leaves edit mode. (See ApplyMenuAction + the input handlers.)
static bool gEditMode = false;
// Edit-mode focus: the cursor can sit on an ITEM (default) or on the CATEGORY tab itself (so a
// category can be edited / deleted / moved). Move mode repositions the focused item/category live.
// gEditOnAddCat = the cursor is on the "+" add-category slot at the end of the category row (drawn where
// the Settings gear normally sits, which is hidden in edit mode). The end-of-item-list "+" add slot is
// handled through the active-list count (ActiveCount adds one in edit mode) - see ActiveEntry.
static bool gEditFocusCat = false;
static bool gMoveMode      = false;
static bool gEditOnAddCat  = false;
// Screen centre of the focused element (set each frame in DrawMenu) so the edit context popup can be
// anchored beside it instead of covering the whole screen. gEditAnchorHalf ~ the element's half-size.
static float gEditAnchorX = 0.0f, gEditAnchorY = 0.0f, gEditAnchorHalf = 0.0f;
static void OpenContextMenu();                // fwd: opens the edit context popup for the current item
static void LeaveEditMode();                  // fwd: exit edit mode + clear its sub-states
// EditNav (edit-mode navigation) is forward-declared just after the MenuAction enum below, since it
// takes a MenuAction.

// --- ported easing (gfx_animation.c). f(t,b,c,d): t=elapsed b=begin c=change d=duration ---
static float EaseOutQuad(float t, float b, float c, float d) { t /= d; return -c * t * (t - 2.0f) + b; }
static float EaseOutCirc(float t, float b, float c, float d) { float k = t / d - 1.0f; return c * sqrtf(1.0f - k * k) + b; }
static float EaseLinear (float t, float b, float c, float d) { return c * (t / d) + b; }   // steady fade

static const float XMB_DELAY   = 166.66667f;  // ms, standard XMB tween duration
static const float kListFadeMs = 380.0f;      // ms, standalone "Fade" list-switch (longer + linear so it reads as a fade, not a pop)

// A single tweened scalar. XY moves use out_quad (XMB_EASING_XY).
struct Tween
{
    float cur = 0, from = 0, to = 0, elapsed = 0, dur = 0;
    bool  anim = false;
    float (*easeFn)(float, float, float, float) = EaseOutQuad; // XMB_EASING_XY default; alpha uses out_circ
    void set(float v)                 { cur = from = to = v; anim = false; }
    void retarget(float target, float d)
    {
        if (anim && to == target) return;   // already heading there
        if (!anim && cur == target) return; // already there
        from = cur; to = target; elapsed = 0; dur = d; anim = true;
    }
    void update(float dtMs)
    {
        if (!anim) return;
        elapsed += dtMs;
        if (elapsed >= dur) { cur = to; anim = false; return; }
        cur = easeFn(elapsed, from, to - from, dur);
    }
};
static Tween gCatTween;   // horizontal category scroll (target = gCatSel)

// Fixed-placement slide tweens (continuous slot units). *ScrollT ease toward the integer scroll
// offset so the whole list glides one slot when it has to scroll; *SelT ease toward the current
// selection index so the highlight box glides between elements. Highlight visual slot = SelT - ScrollT
// (so during an edge-scroll, where sel and scroll move together, the box stays put and the list slides
// under it; during a plain move within the window, the box glides and the list stays put).
static Tween gFixItemScrollT, gFixItemSelT, gFixCatScrollT, gFixCatSelT;

// --- menu-layout tuning (batch 10, config "menu"): spacing multipliers + title controls ---
// Multipliers on the ported XMB layout ratios; 1.0 = the classic look. Spacing multipliers are
// applied in ComputeLayout (so every consumer picks them up); title sizes in CreateMenuFormats;
// the category / item gap and the title on-off in DrawMenu. All clamped 0.10-2.0 on load. Declared
// here (before ComputeLayout) because ComputeLayout reads the spacing multipliers.
// Menu-layout tuning is stored SEPARATELY per (orientation x placement) because the four layouts are
// geometrically different - a value that looks right in horizontal-scrolling is wrong in vertical-fixed.
// spItem       = spacing between submenu items (item axis)
// spCatItem    = gap between the category row/column and the item list (additive)
// spCat        = spacing between main-menu (category) elements (category axis)
// spIconTitle  = distance between an icon and its title
// itemTitleScale / catTitleScale = text sizes; itemTitleShow / catTitleShow = on/off
struct LayoutTune {
    float spItem = 1.0f, spCatItem = 1.0f, spCat = 1.0f, spIconTitle = 1.0f;
    float focusGap = 1.0f;       // scrolling only: extra gap between the focused item and its neighbours
    float catIconTitle = 1.0f;   // gap between a CATEGORY icon and its title (main menu)
    float itemTitleScale = 1.0f, catTitleScale = 1.0f;
    float catIconScale = 1.0f, itemIconScale = 1.0f;   // draw-size multipliers for category / item icons
    bool  itemTitleShow = true, catTitleShow = true;
    bool  catTitleAll = false;   // show EVERY category title (not only the active one), under each icon
    int   alignH = 1;            // fixed placement: block screen-X align 0=Left 1=Center 2=Right
    int   alignV = 1;            // fixed placement: block screen-Y align 0=Top  1=Center 2=Bottom
    bool  subReverse = false;    // Part C2: reverse the submenu flow (up instead of down in horizontal,
                                 // left instead of right in vertical) - applies to every category in this mode
    int   listAnim = 1;          // list-switch animation for THIS mode: 0 None / 1 Slide (default) / 2 Fade / 3 Axis slide
};
// Index = (orientation ? 2 : 0) + (fixed ? 1 : 0): 0 horiz-scroll, 1 horiz-fixed, 2 vert-scroll, 3 vert-fixed.
static LayoutTune gTune[4];
static LayoutTune& CurTune() { return gTune[(gVertical ? 2 : 0) + (gFixedLayout ? 1 : 0)]; }

// Menu-wide scale (RA menu_scale_factor). ONE global multiplier that grows/shrinks the WHOLE menu
// together - icons, text, spacing and margins - because everything below is derived from L.scale.
// Separate from the per-element multipliers in LayoutTune. 1.0 = classic look. Config "menu.scale".
static float gMenuScale = 1.0f;

// --- layout (ported PS3 xmb_layout_ps3 / xmb_layout_common ratios, verbatim from xmb.c) ---
struct MenuLayout
{
    float scale, iconSize, spacingH, spacingV, marginTop, marginLeft, labelLeft, fontSize;
    float catPitch, itemPitchV;   // catPitch = category-axis spacing (both orientations, x spCat);
                                  // itemPitchV = vertical item-strip pitch (x spItem)
    float labelTop, activeItemFactor, aboveItemOffset, underItemOffset, shadowOffset;
    float titleLeft, titleTop;
};
static MenuLayout ComputeLayout(int W, int /*H*/)
{
    MenuLayout L;
    const float menuScale = gMenuScale;           // menu_scale_factor (user "Menu scale" setting)
    L.scale     = menuScale * (float)W / 1920.0f;  // xmb_get_scale_factor (PS3 layout)
    // cap stays 1.0 so the user scale flows UNIFORMLY into every derived size (icons + spacing +
    // margins all grow/shrink together). At the default menuScale=1.0 this is byte-identical to before.
    const float cap = 1.0f;
    L.iconSize  = 128.0f * L.scale / cap;
    L.spacingH  = 192.0f * L.scale / cap;
    L.spacingV  = 64.0f  * L.scale;
    L.marginTop = (256.0f + 16.0f) * L.scale / cap;
    L.marginLeft= 336.0f * L.scale / cap;
    L.labelLeft = 85.0f  * L.scale / cap;
    L.fontSize  = 32.0f  * L.scale;
    if (L.fontSize < 7.0f) L.fontSize = 7.0f;
    L.labelTop  = L.fontSize / 3.0f;              // margins_label_top
    // xmb_layout_ps3 row offsets (scale_cap = 1): the active item is splayed from its neighbours.
    L.activeItemFactor = 3.0f;   // active_item_factor
    L.aboveItemOffset  = -1.0f;  // above_item_offset
    L.underItemOffset  = 5.0f;   // under_item_offset
    L.shadowOffset     = 4.0f * L.scale;                 // xmb_layout_common: clamp(4*scale, 1, 2)
    if (L.shadowOffset < 1.0f) L.shadowOffset = 1.0f;
    if (L.shadowOffset > 2.0f) L.shadowOffset = 2.0f;
    // margins_title (xmb.c): title_margin default 3 -> *10 = 30; near the top-left corner.
    L.titleLeft = (30.0f + 4.0f) * L.scale;                                  // margins_title_left
    L.titleTop  = 30.0f * L.scale + (L.fontSize - (L.fontSize / 6.0f) * L.scale); // margins_title_top (baseline)
    // User layout-tuning multipliers for the CURRENT (orientation x placement) layout. Item-axis and
    // category-axis pitches are kept separate so a knob only affects what it should (e.g. in vertical the
    // category column and the item strip both used spacingH before, so "main menu spacing" bled into the
    // items - fixed here by giving each its own scaled pitch). spacingH stays the unscaled base.
    const LayoutTune& T = CurTune();
    L.spacingV  *= T.spItem;                 // horizontal item COLUMN pitch (via XmbItemY / edge fade)
    L.labelLeft *= T.spIconTitle;            // horizontal icon-to-title distance
    L.catPitch   = L.spacingH * T.spCat;     // category-axis spacing (both orientations)
    L.itemPitchV = L.spacingH * T.spItem;    // vertical item-strip pitch
    return L;
}

// Per-item animation, mirroring RA's separate per-node anim entries (each property its own easing):
//   gItemYT = node->y in px (XMB_EASING_XY = out_quad)
//   gItemZT = zoom  selectedness 0..1 (out_quad)
//   gItemAT = alpha selectedness 0..1 (XMB_EASING_ALPHA = out_circ)
//   gItemXT = node->x parallax offset in px (XMB_EASING_XY = out_quad) - drives the list-switch slide
static std::vector<Tween> gItemYT;
static std::vector<Tween> gItemZT;
static std::vector<Tween> gItemAT;
static std::vector<Tween> gItemXT;

// Whole-list fade on a category switch. RA fades each new node's alpha from 0 -> target (out_circ)
// over XMB_DELAY; a single 0..1 multiplier reproduces that (all nodes rise together). Default 1.0
// so the list is visible at startup.
static Tween gListFade;
// List-switch animation is a per-mode setting (LayoutTune.listAnim, via CurTune()): 0 None / 1 Slide
// (original XMB parallax across the menu bar, default) / 2 Fade only / 3 Axis slide (along the item-list
// axis). gListSlideAxis is a transient render flag set by StartListSwitch telling DrawMenu which axis the
// slide offset applies to: 0 = X, 1 = Y.
static int gListSlideAxis = 0;
// Fixed placement defers the list-switch animation until the category selector finishes gliding to the
// new column: MoveCategory hides the list and arms these; UpdateListSwitch (main loop) fires
// StartListSwitch(gListSwitchDir) once gListSwitchDelayMs runs out. gListSwitchDelayMs < 0 = nothing pending.
static int   gListSwitchDir     = 0;
static float gListSwitchDelayMs = -1.0f;

// Ported xmb_item_y (PS3, top-level depth): vertical offset of item i from margins_screen_top, px.
static float XmbItemY(const MenuLayout& L, int i, int current)
{
    // focusGap widens (or narrows) the space between the active item and the rest: neighbours above are
    // pushed further up, neighbours below further down. focusGap = 1.0 keeps the stock XMB splay.
    float extra = L.spacingV * (CurTune().focusGap - 1.0f);
    if (i <  current) return L.spacingV * ((float)(i - current) + L.aboveItemOffset) - extra;
    if (i == current) return L.spacingV *  L.activeItemFactor;
    return               L.spacingV * ((float)(i - current) + L.underItemOffset) + extra;
}

// Fixed-placement vertical pitch for the horizontal-orientation item COLUMN. Scrolling XMB hides
// icon overlap via passive zoom; fixed placement can show a full-size selected item, so give the
// column a pitch a bit larger than the icon so a selected item never covers the rows above/below.
static float FixedColPitchV(const MenuLayout& L) { return L.iconSize * 1.25f * CurTune().spItem; }
