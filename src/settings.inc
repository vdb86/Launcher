// =============================================================================
// Settings screen (UI_SETTINGS): the single, controller-navigable place to change every option and
// to reach the menu editor. Replaces the old temporary dev hotkeys - nothing is changed by stray
// keys any more. Up/Down pick a row (wrap), Left/Right adjust a value, OK toggles / enters, B/Esc
// closes. Every change is persisted via SaveConfig().
// =============================================================================
// Rows are grouped under section headers (SH_*), RetroArch-settings style. The enum + gSettingsSel
// are declared earlier (near the menu globals) so the Settings tab can be driven from DrawMenu /
// ApplyMenuAction; only the function bodies live here.
// --- colour picker (UI_COLOR): an HSV square + hue bar, controller/keyboard driven ---------------
// Replaces the old per-channel R/G/B rows. Opened from a Settings row for the ribbon colour or the
// custom-theme top/bottom colour; edits live and returns to the Settings popup on OK / Cancel.
enum ColorTarget { COL_RIBBON, COL_THEMETOP, COL_THEMEBOT };
static ColorTarget gColorTarget = COL_RIBBON;
static float gColH = 0.0f, gColS = 0.0f, gColV = 0.0f;   // current hue (0-360) / saturation / value (0-1)
static int   gColFocus = 0;                              // 0 = SV square, 1 = hue bar (left of it)
static float gColOrig[3] = { 0,0,0 };                    // RGB to restore on cancel

static void HsvToRgb(float h, float s, float v, float& r, float& g, float& b)
{
    h = fmodf(h, 360.0f); if (h < 0) h += 360.0f;
    float c = v * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
    float rr, gg, bb;
    if      (h <  60) { rr = c; gg = x; bb = 0; }
    else if (h < 120) { rr = x; gg = c; bb = 0; }
    else if (h < 180) { rr = 0; gg = c; bb = x; }
    else if (h < 240) { rr = 0; gg = x; bb = c; }
    else if (h < 300) { rr = x; gg = 0; bb = c; }
    else              { rr = c; gg = 0; bb = x; }
    r = rr + m; g = gg + m; b = bb + m;
}
static void RgbToHsv(float r, float g, float b, float& h, float& s, float& v)
{
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
    v = mx; s = (mx <= 0.0f) ? 0.0f : d / mx;
    if (d <= 0.0f) { h = gColH; return; }                // keep the current hue for greys (no hue info)
    if      (mx == r) h = 60.0f * fmodf((g - b) / d, 6.0f);
    else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
    else              h = 60.0f * ((r - g) / d + 4.0f);
    if (h < 0) h += 360.0f;
}
static void ColorGetRGB(float rgb[3])
{
    if      (gColorTarget == COL_RIBBON)  { rgb[0]=gRibbonColor.x; rgb[1]=gRibbonColor.y; rgb[2]=gRibbonColor.z; }
    else if (gColorTarget == COL_THEMETOP){ rgb[0]=gThemeTop[0];   rgb[1]=gThemeTop[1];   rgb[2]=gThemeTop[2];   }
    else                                  { rgb[0]=gThemeBot[0];   rgb[1]=gThemeBot[1];   rgb[2]=gThemeBot[2];   }
}
static void ColorSetRGB(float r, float g, float b)
{
    if (gColorTarget == COL_RIBBON) { gRibbonColor.x=r; gRibbonColor.y=g; gRibbonColor.z=b; }
    else {
        float* t = (gColorTarget == COL_THEMETOP) ? gThemeTop : gThemeBot;
        t[0]=r; t[1]=g; t[2]=b; BuildCustomTheme(); UpdateGradientVB(gTheme);
    }
}
static void ColorApplyLive() { float r,g,b; HsvToRgb(gColH, gColS, gColV, r,g,b); ColorSetRGB(r,g,b); }
static void ColorStart(ColorTarget t)
{
    gColorTarget = t; ColorGetRGB(gColOrig);
    RgbToHsv(gColOrig[0], gColOrig[1], gColOrig[2], gColH, gColS, gColV);
    gColFocus = 1; gUi = UI_COLOR;                       // land on the hue bar first, then Right -> square
}
static void ColorNav(MenuAction a)
{
    if (a == ACT_CANCEL) { ColorSetRGB(gColOrig[0], gColOrig[1], gColOrig[2]); gUi = UI_SETTINGS; return; }
    if (a == ACT_OK)     { ColorApplyLive(); SaveConfig(); gUi = UI_SETTINGS; return; }
    if (gColFocus == 1)                                  // hue bar (left). Up = toward the top (0 deg).
    {
        if      (a == ACT_UP)    { gColH -= 6.0f; if (gColH < 0)    gColH += 360; }
        else if (a == ACT_DOWN)  { gColH += 6.0f; if (gColH >= 360) gColH -= 360; }
        else if (a == ACT_RIGHT) { gColFocus = 0; }      // into the saturation/value square
    }
    else                                                 // saturation (x) x value (y) square
    {
        if      (a == ACT_LEFT)  { if (gColS <= 0.001f) gColFocus = 1; else { gColS -= 0.03f; if (gColS < 0) gColS = 0; } }
        else if (a == ACT_RIGHT) { gColS += 0.03f; if (gColS > 1) gColS = 1; }
        else if (a == ACT_UP)    { gColV += 0.03f; if (gColV > 1) gColV = 1; }
        else if (a == ACT_DOWN)  { gColV -= 0.03f; if (gColV < 0) gColV = 0; }
    }
    ColorApplyLive();
}

// Ribbon-variant + Theme only matter when the Background is set to Ribbon, so hide them otherwise
// (RetroArch-style: selecting Ribbon reveals which-ribbon + which-theme).
static bool SettingIsHidden(int r)
{
    if (r == S_BGIMAGE)                return !BgIsImage();     // only when Background = Image
    if (r == S_RIBBON || r == S_THEME) return !BgIsRibbon();
    if (r == S_RIBBONCYCLE || r == S_RIBBONOPACITY || r == S_RIBBONSHEEN) return !RibbonIsCustom();  // Custom Colour Ribbon only
    if (r == S_RIBBONCOL) return !RibbonIsCustom() || gRibbonCycle;   // colour pick hidden while cycling (no effect then)
    if (r == S_THEMETOPCOL || r == S_THEMEBOTCOL)
        return !(BgIsRibbon() && gTheme == kThemeCount);       // only when Theme = Custom
    if (r == S_ENLARGE)                return !gFixedLayout;   // only meaningful in fixed placement
    if (r == S_ALIGNH) return !(gFixedLayout && !gVertical);   // anchor for a HORIZONTAL fixed menu only
    if (r == S_ALIGNV) return !(gFixedLayout &&  gVertical);   // anchor for a VERTICAL fixed menu only
    if (r == S_FOCUSGAP)               return gFixedLayout;    // splay gap only exists in scrolling menus
    if (r == S_SUBDIR)                 return gMenu.empty();   // per-mode submenu flow direction
    if (r == S_CATTITLE_ALL)           return !CurTune().catTitleShow; // only when category titles are shown
    if (r == S_DATEBOLD || r == S_DATEFMT) return !gDateEnabled; // date-only options
    if (r == S_DATESEP) return !gDateEnabled || gDateFormat < 10; // numeric-date separator only
    return false;
}
static const wchar_t* SettingLabel(int r)
{
    switch (r)
    {
    case SH_APPEAR:    return L"APPEARANCE";
    case S_BG:         return L"Background";
    case S_BGIMAGE:    return L"Background image...";
    case S_RIBBON:     return L"Ribbon style";
    case S_RIBBONCOL:  return L"Ribbon colour...";
    case S_RIBBONCYCLE:return L"Ribbon colour cycle";
    case S_RIBBONOPACITY: return L"Ribbon opacity";
    case S_RIBBONSHEEN:   return L"Ribbon sheen";
    case S_THEME:      return L"Theme";
    case S_THEMETOPCOL:return L"Custom top colour...";
    case S_THEMEBOTCOL:return L"Custom bottom colour...";
    case S_ORIENT:     return L"Orientation";
    case S_PLACE:      return L"Menu placement";
    case S_ENLARGE:    return L"Enlarge selected";
    case S_UIFONT:     return L"Font";
    case S_CLOCKSIZE:  return L"Clock size";
    case S_CLOCKCORNER:return L"Clock position";
    case S_CLOCKBOLD:  return L"Bold time";
    case S_DATEON:     return L"Show date";
    case S_DATEBOLD:   return L"Bold date";
    case S_DATEFMT:    return L"Date format";
    case S_DATESEP:    return L"Number separator";
    case S_HOURFMT:    return L"Hour format";
    case S_BLINKCOLON: return L"Blinking colon";
    case SH_SAVER:     return L"SCREENSAVER";
    case S_SAVER:      return L"Screensaver";
    case S_TIMEOUT:    return L"Idle delay";
    case S_DIMEN:      return L"Dim screen";
    case S_DIM:        return L"Dim amount";
    case S_SAVERCLOCK: return L"Clock";
    case SH_SYSTEM:    return L"SYSTEM";
    case S_KEEPAWAKE:  return L"Keep display awake";
    case S_RETURNHOME: return L"Return to launcher";
    case S_AUTOSTART:  return L"Start with Windows";
    case S_KIOSK:      return L"Kiosk (replace Windows shell)";
    case SH_MENU:      return L"MENU";
    case S_ALIGNH:        return L"Horizontal align";
    case S_ALIGNV:        return L"Vertical align";
    case S_SUBDIR:        return L"Submenu direction";
    case S_LISTANIM:      return L"List animation";
    case S_MENUSCALE:     return L"Menu scale";
    case S_SP_ITEM:       return L"Item spacing";
    case S_FOCUSGAP:      return L"Focused item gap";
    case S_SP_CATITEM:    return L"Category / item gap";
    case S_SP_CAT:        return L"Main menu spacing";
    case S_SP_ICONTITLE:  return L"Icon / title distance";
    case S_CAT_ICONTITLE: return L"Category icon / title gap";
    case S_CATICON_SIZE:  return L"Category icon size";
    case S_ITEMICON_SIZE: return L"Item icon size";
    case S_MENUPOS:       return L"Menu position...";
    case S_MENUPOSRESET:  return L"Reset menu position";
    case S_ITEMTITLE_SIZE:return L"Item title size";
    case S_ITEMTITLE_SHOW:return L"Item titles";
    case S_CATTITLE_SIZE: return L"Category title size";
    case S_CATTITLE_SHOW: return L"Category title";
    case S_CATTITLE_ALL:  return L"Show all category titles";
    case S_EDIT:       return L"Edit menu items / categories...";
    case S_CLOSE:      return L"Back to menu";
    }
    return L"";
}
// One-line description for the highlighted row (RetroArch-style sublabel).
static const wchar_t* SettingDesc(int r)
{
    switch (r)
    {
    case S_BG:         return L"Backdrop: None, an animated background, the XMB Ribbon, or your own Image.";
    case S_BGIMAGE:    return L"Pick a full-screen background image (OK to browse).";
    case S_RIBBON:     return L"Which ribbon to show (Background must be Ribbon).";
    case S_RIBBONCOL:  return L"Custom Colour Ribbon: pick its colour (OK opens the colour map).";
    case S_RIBBONCYCLE:return L"Custom Colour Ribbon: slowly cycle the ribbon through the colour spectrum.";
    case S_RIBBONOPACITY: return L"Custom Colour Ribbon: how opaque the ribbon looks (lower = more see-through).";
    case S_RIBBONSHEEN:   return L"Custom Colour Ribbon: fold-sheen divisor (LOWER = brighter, more solid folds).";
    case S_THEME:      return L"Colour gradient behind the ribbon (pick Custom to set your own).";
    case S_THEMETOPCOL:return L"Custom theme: the gradient's top colour (OK opens the colour map).";
    case S_THEMEBOTCOL:return L"Custom theme: the gradient's bottom colour (OK opens the colour map).";
    case S_ORIENT:     return L"Lay the menu out horizontally or vertically.";
    case S_PLACE:      return L"Scrolling: items move past a fixed cursor. Fixed: cursor moves, list stays.";
    case S_ENLARGE:    return L"Fixed placement: On grows the selected icon; Off keeps all icons one size and just brightens the selected.";
    case S_UIFONT:     return L"Font used across the whole menu and clock.";
    case S_CLOCKSIZE:  return L"Size of the on-screen clock.";
    case S_CLOCKCORNER:return L"Which screen corner the menu clock sits in.";
    case S_CLOCKBOLD:  return L"Draw the time in bold.";
    case S_DATEON:     return L"Show the date line under the time.";
    case S_DATEBOLD:   return L"Draw the date in bold.";
    case S_DATEFMT:    return L"How the date is written.";
    case S_DATESEP:    return L"Separator for the numeric date formats (dot, dash or slash).";
    case S_HOURFMT:    return L"12-hour, 24-hour, or the system format.";
    case S_BLINKCOLON: return L"Blink the colon in the time (one second on, one second off).";
    case S_SAVER:      return L"Fade the menu to a screensaver after a period of no input.";
    case S_TIMEOUT:    return L"Seconds of inactivity before the screensaver starts.";
    case S_DIMEN:      return L"Also darken the screen during the screensaver.";
    case S_DIM:        return L"How dark the screensaver gets (100% = black).";
    case S_SAVERCLOCK: return L"Show a clock during the screensaver; it hops corners each minute.";
    case S_KEEPAWAKE:  return L"Stop Windows sleeping / blanking while the launcher is in front.";
    case S_RETURNHOME: return L"Come back to the front automatically when every app is closed, hidden or minimised (the desktop would otherwise show).";
    case S_AUTOSTART:  return L"Launch automatically when you sign in to Windows.";
    case S_KIOSK:      return L"Boot straight into the launcher instead of the Windows desktop (replaces the shell). Recovery: Ctrl+Shift+Esc opens Task Manager; turn this off to restore the desktop.";
    case S_ALIGNH:        return L"Fixed placement: anchor the horizontal menu to the left, centre or right.";
    case S_ALIGNV:        return L"Fixed placement: anchor the vertical menu to the top, centre or bottom.";
    case S_SUBDIR:        return L"Which way the selected category's items flow out from the menu bar.";
    case S_LISTANIM:      return L"How the item list appears when you switch category: None, Slide, Fade, or Axis slide.";
    case S_MENUSCALE:     return L"Scale the whole menu at once - icons, text and spacing together.";
    case S_SP_ITEM:       return L"Spacing between items in a submenu.";
    case S_FOCUSGAP:      return L"Extra gap between the focused item and the others (scrolling menus).";
    case S_SP_CATITEM:    return L"Gap between the category row and its item list.";
    case S_SP_CAT:        return L"Spacing between the main-menu (category) elements.";
    case S_SP_ICONTITLE:  return L"Distance between an item's icon and its title.";
    case S_CAT_ICONTITLE: return L"Distance between a category icon and its title (main menu).";
    case S_CATICON_SIZE:  return L"Size of the main-menu category icons.";
    case S_ITEMICON_SIZE: return L"Size of the item icons in a submenu.";
    case S_MENUPOS:       return L"Move the whole menu around the screen (OK starts; arrows nudge, OK save, B cancel).";
    case S_MENUPOSRESET:  return L"Put the whole menu back to its default position.";
    case S_ITEMTITLE_SIZE:return L"Text size of the item titles.";
    case S_ITEMTITLE_SHOW:return L"Show item titles, or icons only.";
    case S_CATTITLE_SIZE: return L"Text size of the active category's title.";
    case S_CATTITLE_SHOW: return L"Show the active category's title below its icon.";
    case S_CATTITLE_ALL:  return L"Label every category (not just the active one) under its icon.";
    case S_EDIT:       return L"Add, edit, reorder or delete categories and items.";
    case S_CLOSE:      return L"Return to the menu.";
    }
    return L"";
}
static std::wstring SettingValue(int r)
{
    wchar_t b[64];
    switch (r)
    {
    case S_BG:         return (gBgChoice == 0)                ? std::wstring(L"None")
                            : (gBgChoice == BgRibbonChoice()) ? std::wstring(L"Ribbon")
                            : (gBgChoice == BgImageChoice())  ? std::wstring(L"Image")
                            : Utf8ToWide(gPipes[gBgList[gBgChoice - 1]].name);
    case S_BGIMAGE:    return gBgImagePath.empty() ? std::wstring(L"(none)")
                            : gBgImagePath.substr(gBgImagePath.find_last_of(L"\\/") + 1);
    case S_RIBBON:     return (gRibbonChoice >= 0 && gRibbonChoice < (int)gRibbonList.size())
                            ? Utf8ToWide(gPipes[gRibbonList[gRibbonChoice]].name) : std::wstring(L"-");
    case S_RIBBONCOL:  swprintf(b, 64, L"#%02X%02X%02X", Ch255(gRibbonColor.x), Ch255(gRibbonColor.y), Ch255(gRibbonColor.z)); return b;
    case S_RIBBONCYCLE:return gRibbonCycle ? L"On" : L"Off";
    case S_RIBBONOPACITY: swprintf(b, 64, L"%d%%", (int)(gRibbonOpacity * 100.0f + 0.5f)); return b;
    case S_RIBBONSHEEN:   swprintf(b, 64, L"%.1f", gRibbonSheen); return b;
    case S_MENUPOS:      return L"";      // an action (opens the nudge mode), not a value to cycle
    case S_THEME:      return (gTheme == kThemeCount) ? std::wstring(L"Custom")
                            : (gTheme >= 0 && gTheme < kThemeCount) ? Utf8ToWide(kThemes[gTheme].name)
                            : std::wstring(L"Dark");
    case S_THEMETOPCOL:swprintf(b, 64, L"#%02X%02X%02X", Ch255(gThemeTop[0]), Ch255(gThemeTop[1]), Ch255(gThemeTop[2])); return b;
    case S_THEMEBOTCOL:swprintf(b, 64, L"#%02X%02X%02X", Ch255(gThemeBot[0]), Ch255(gThemeBot[1]), Ch255(gThemeBot[2])); return b;
    case S_ORIENT:     return gVertical ? L"Vertical" : L"Horizontal";
    case S_PLACE:      return gFixedLayout ? L"Fixed" : L"Scrolling";
    case S_ENLARGE:    return gEnlargeSelected ? L"On" : L"Off";
    case S_UIFONT:     return UiFont();
    case S_CLOCKSIZE:  swprintf(b, 64, L"%.1fx", gClockScale); return b;
    case S_CLOCKBOLD:  return gClockBold ? L"On" : L"Off";
    case S_DATEON:     return gDateEnabled ? L"On" : L"Off";
    case S_DATEBOLD:   return gDateBold ? L"On" : L"Off";
    case S_DATEFMT:    return DateFormatExample(gDateFormat);   // live example from 29 Jul 2024
    case S_DATESEP:    return gDateSep == 1 ? L"- (dash)" : gDateSep == 2 ? L"/ (slash)" : L". (dot)";
    case S_CLOCKCORNER: return gClockCorner == CLOCK_TL ? L"Top-left"
                            : gClockCorner == CLOCK_BR ? L"Bottom-right"
                            : gClockCorner == CLOCK_BL ? L"Bottom-left" : L"Top-right";
    case S_BLINKCOLON: return gBlinkColon ? L"On" : L"Off";
    case S_HOURFMT:    return gHourFormat == 1 ? L"12-hour (localized)"
                            : gHourFormat == 2 ? L"24-hour"
                            : gHourFormat == 3 ? L"12-hour (English)" : L"System format";
    case S_SAVER:      return gScreensaverEnabled ? L"On" : L"Off";
    case S_TIMEOUT:    swprintf(b, 64, L"%d s", (int)(gIdleTimeoutMs / 1000.0f + 0.5f)); return b;
    case S_DIMEN:      return gDimEnabled ? L"On" : L"Off";
    case S_DIM:        swprintf(b, 64, gDimEnabled ? L"%d%%" : L"%d%% (off)", gSaverDimPercent); return b;
    case S_SAVERCLOCK: return !gSaverClockEnabled ? L"Off"
                            : gSaverClockMotion == 1 ? L"Counterclockwise"
                            : gSaverClockMotion == 2 ? L"Random" : L"Clockwise";
    case S_KEEPAWAKE:  return gInhibitSleep ? L"On" : L"Off";
    case S_RETURNHOME: return gReturnHome ? L"On" : L"Off";
    case S_AUTOSTART:  return AutostartEnabled() ? L"On" : L"Off";
    case S_KIOSK:      return KioskEnabled()     ? L"On" : L"Off";
    case S_ALIGNH:        return (CurTune().alignH == 0) ? L"Left" : (CurTune().alignH == 2) ? L"Right"  : L"Center";
    case S_ALIGNV:        return (CurTune().alignV == 0) ? L"Top"  : (CurTune().alignV == 2) ? L"Bottom" : L"Center";
    case S_SUBDIR: {
        bool rev = CurTune().subReverse;
        return gVertical ? (rev ? L"Left" : L"Right") : (rev ? L"Up" : L"Down");
    }
    case S_LISTANIM: { int a = CurTune().listAnim;
        return a == 0 ? L"None" : a == 2 ? L"Fade" : a == 3 ? L"Axis slide" : L"Slide"; }
    case S_MENUSCALE:     swprintf(b, 64, L"%.2fx", gMenuScale);            return b;
    case S_SP_ITEM:       swprintf(b, 64, L"%.2fx", CurTune().spItem);       return b;
    case S_FOCUSGAP:      swprintf(b, 64, L"%.2fx", CurTune().focusGap);     return b;
    case S_SP_CATITEM:    swprintf(b, 64, L"%.2fx", CurTune().spCatItem);    return b;
    case S_SP_CAT:        swprintf(b, 64, L"%.2fx", CurTune().spCat);        return b;
    case S_SP_ICONTITLE:  swprintf(b, 64, L"%.2fx", CurTune().spIconTitle);  return b;
    case S_CAT_ICONTITLE: swprintf(b, 64, L"%.2fx", CurTune().catIconTitle); return b;
    case S_CATICON_SIZE:  swprintf(b, 64, L"%.2fx", CurTune().catIconScale);  return b;
    case S_ITEMICON_SIZE: swprintf(b, 64, L"%.2fx", CurTune().itemIconScale); return b;
    case S_ITEMTITLE_SIZE:swprintf(b, 64, L"%.2fx", CurTune().itemTitleScale); return b;
    case S_ITEMTITLE_SHOW:return CurTune().itemTitleShow ? L"On" : L"Off";
    case S_CATTITLE_SIZE: swprintf(b, 64, L"%.2fx", CurTune().catTitleScale);  return b;
    case S_CATTITLE_SHOW: return CurTune().catTitleShow ? L"On" : L"Off";
    case S_CATTITLE_ALL:  return CurTune().catTitleAll ? L"On" : L"Off";
    }
    return L"";
}
// delta: -1 = Left, +1 = Right, 0 = OK. Toggles react to any of the three; numeric/enum rows to
// Left/Right (OK steps enums forward); the action rows only to OK.
static void SettingApply(int r, int delta)
{
    switch (r)
    {
    case S_BG: {                                          // None / animated backgrounds / Ribbon / Image
        int n = BgChoiceCount(), step = (delta == 0) ? 1 : delta;
        gBgChoice = ((gBgChoice + step) % n + n) % n;
        ApplyBgSelection(); SaveConfig(); break;
    }
    case S_BGIMAGE:  if (delta == 0) FilesStart(EF_BG_IMAGE, true); break;   // browse for a background image
    case S_RIBBON: {                                      // which ribbon (only meaningful in Ribbon mode)
        int n = (int)gRibbonList.size(); if (n <= 0) break;
        int step = (delta == 0) ? 1 : delta;
        gRibbonChoice = ((gRibbonChoice + step) % n + n) % n;
        ApplyBgSelection(); SaveConfig(); break;
    }
    case S_RIBBONCOL: if (delta == 0) ColorStart(COL_RIBBON); break;   // OK opens the colour map
    case S_RIBBONCYCLE:                                                // toggle slow hue cycling
        gRibbonCycle = !gRibbonCycle;
        if (gRibbonCycle) { float h, s, v; RgbToHsv(gRibbonColor.x, gRibbonColor.y, gRibbonColor.z, h, s, v); gRibbonCycleHue = h; }  // start from the current colour
        SaveConfig(); break;
    case S_RIBBONOPACITY: if (delta) { gRibbonOpacity += delta * 0.05f; if (gRibbonOpacity < 0.05f) gRibbonOpacity = 0.05f; if (gRibbonOpacity > 1.0f) gRibbonOpacity = 1.0f; SaveConfig(); } break;
    case S_RIBBONSHEEN:   if (delta) { gRibbonSheen += delta * 0.5f; if (gRibbonSheen < 1.0f) gRibbonSheen = 1.0f; if (gRibbonSheen > 20.0f) gRibbonSheen = 20.0f; SaveConfig(); } break;
    case S_THEME: {                                       // ...themes + a "Custom" entry at index kThemeCount
        int step = (delta == 0) ? 1 : delta, n = kThemeCount + 1;
        gTheme = ((gTheme + step) % n + n) % n;
        if (gTheme == kThemeCount) BuildCustomTheme();
        UpdateGradientVB(gTheme); SaveConfig(); break;
    }
    case S_THEMETOPCOL: if (delta == 0) ColorStart(COL_THEMETOP); break;   // OK opens the colour map
    case S_THEMEBOTCOL: if (delta == 0) ColorStart(COL_THEMEBOT); break;
    case S_ORIENT:    SetVertical(!gVertical);        SaveConfig(); break;
    case S_PLACE:     SetPlacement(!gFixedLayout);    SaveConfig(); break;
    case S_ENLARGE:   gEnlargeSelected = !gEnlargeSelected; SaveConfig(); break;
    case S_UIFONT: {
        int step = (delta == 0) ? 1 : delta;
        gUiFontIdx = ((gUiFontIdx + step) % kUiFontCount + kUiFontCount) % kUiFontCount;
        CreateMenuFormats(); SaveConfig(); break;
    }
    case S_CLOCKSIZE:
        if (delta != 0) { gClockScale += delta * 0.1f;
            if (gClockScale < 0.4f) gClockScale = 0.4f; if (gClockScale > 3.0f) gClockScale = 3.0f;
            CreateMenuFormats(); SaveConfig(); }
        break;
    case S_CLOCKBOLD: gClockBold = !gClockBold; CreateMenuFormats(); SaveConfig(); break;
    case S_CLOCKCORNER: {
        int step = (delta == 0) ? 1 : delta;
        gClockCorner = ((gClockCorner + step) % 4 + 4) % 4; SaveConfig(); break; // TL -> TR -> BR -> BL
    }
    case S_DATEON:    gDateEnabled = !gDateEnabled; SaveConfig(); break;
    case S_DATEBOLD:  gDateBold = !gDateBold; CreateMenuFormats(); SaveConfig(); break;
    case S_DATEFMT: {
        int step = (delta == 0) ? 1 : delta;
        gDateFormat = ((gDateFormat + step) % 13 + 13) % 13; SaveConfig(); break;   // 13 date formats (0-12)
    }
    case S_DATESEP: {
        int step = (delta == 0) ? 1 : delta;
        gDateSep = ((gDateSep + step) % 3 + 3) % 3; SaveConfig(); break;   // . / - / /
    }
    case S_BLINKCOLON: gBlinkColon = !gBlinkColon; SaveConfig(); break;
    case S_HOURFMT: {
        int step = (delta == 0) ? 1 : delta;
        gHourFormat = ((gHourFormat + step) % 4 + 4) % 4; SaveConfig(); break;   // 4 hour formats (incl. English 12h)
    }
    case S_SAVER:     gScreensaverEnabled = !gScreensaverEnabled; SaveConfig(); break;
    case S_TIMEOUT:
        if (delta != 0) {                                 // step by 5s, snapped to a clean 5s grid
            int s = (int)(gIdleTimeoutMs / 1000.0f + 0.5f);
            s = ((s + delta * 5) / 5) * 5;
            if (s < 5)    s = 5;
            if (s > 3600) s = 3600;
            gIdleTimeoutMs = (float)s * 1000.0f; SaveConfig();
        }
        break;
    case S_DIMEN:     gDimEnabled = !gDimEnabled; SaveConfig(); break;
    case S_DIM:
        if (delta != 0 && gDimEnabled) { gSaverDimPercent += delta * 10;
            if (gSaverDimPercent < 0) gSaverDimPercent = 0; if (gSaverDimPercent > 100) gSaverDimPercent = 100;
            SaveConfig(); }
        break;
    case S_SAVERCLOCK: {
        int step = (delta == 0) ? 1 : delta;              // OK steps forward
        int cur  = !gSaverClockEnabled ? 0 : gSaverClockMotion + 1;   // 0 off /1 CW /2 CCW /3 random
        cur = ((cur + step) % 4 + 4) % 4;
        gSaverClockEnabled = (cur != 0);
        if (cur != 0) gSaverClockMotion = cur - 1;
        SaveConfig(); break;
    }
    case S_KEEPAWAKE: gInhibitSleep = !gInhibitSleep; KeepAwake(true); SaveConfig(); break;
    case S_RETURNHOME: gReturnHome = !gReturnHome; SaveConfig(); break;
    // Registry-backed toggles (no launcher.json entry): flip on any apply, like Keep-awake above.
    case S_AUTOSTART: SetAutostart(!AutostartEnabled()); break;
    case S_KIOSK:     SetKiosk(!KioskEnabled());         break;  // SetKiosk persists gShellBackup itself
    // Spacing multipliers: Left/Right step by 0.05, clamp 0.10-2.0. Re-snap the item tweens + fixed scroll
    // (these feed off the layout) so the change is reflected without waiting for the next nav.
    case S_ALIGNH:       if (delta) { int& v=CurTune().alignH; v=(v+delta+3)%3; RetargetItems(true); SaveConfig(); } break;
    case S_ALIGNV:       if (delta) { int& v=CurTune().alignV; v=(v+delta+3)%3; RetargetItems(true); SaveConfig(); } break;
    case S_SUBDIR:       CurTune().subReverse = !CurTune().subReverse;   // two-value; toggle on Left/Right/OK (per-mode)
                         RetargetItems(true); SaveConfig(); break;
    case S_LISTANIM: {                                                    // None / Slide / Fade / Axis slide (per-mode)
        int step = (delta == 0) ? 1 : delta;
        int& a = CurTune().listAnim; a = ((a + step) % 4 + 4) % 4; SaveConfig(); break;
    }
    // Menu-wide scale: fonts come from ComputeLayout, so rebuild them (CreateMenuFormats) as well as
    // re-snapping the item/scroll tweens. Global, clamp 0.50-2.00x, step 0.05.
    case S_MENUSCALE:    if (delta) { gMenuScale += delta*0.05f; if(gMenuScale<0.50f)gMenuScale=0.50f; if(gMenuScale>2.0f)gMenuScale=2.0f; CreateMenuFormats(); RetargetItems(true); SaveConfig(); } break;
    case S_SP_ITEM:      if (delta) { float& v=CurTune().spItem;      v+=delta*0.05f; if(v<0.10f)v=0.10f; if(v>2.0f)v=2.0f; RetargetItems(true); SaveConfig(); } break;
    case S_FOCUSGAP:     if (delta) { float& v=CurTune().focusGap;    v+=delta*0.05f; if(v<0.10f)v=0.10f; if(v>2.0f)v=2.0f; RetargetItems(true); SaveConfig(); } break;
    case S_SP_CATITEM:   if (delta) { float& v=CurTune().spCatItem;   v+=delta*0.05f; if(v<0.10f)v=0.10f; if(v>2.0f)v=2.0f; RetargetItems(true); SaveConfig(); } break;
    case S_SP_CAT:       if (delta) { float& v=CurTune().spCat;       v+=delta*0.05f; if(v<0.10f)v=0.10f; if(v>2.0f)v=2.0f; RetargetItems(true); SaveConfig(); } break;
    case S_SP_ICONTITLE: if (delta) { float& v=CurTune().spIconTitle; v+=delta*0.05f; if(v<0.10f)v=0.10f; if(v>2.0f)v=2.0f; SaveConfig(); } break;
    case S_CAT_ICONTITLE:if (delta) { float& v=CurTune().catIconTitle; v+=delta*0.05f; if(v<0.10f)v=0.10f; if(v>2.0f)v=2.0f; SaveConfig(); } break;
    case S_CATICON_SIZE: if (delta) { float& v=CurTune().catIconScale;  v+=delta*0.05f; if(v<0.10f)v=0.10f; if(v>2.0f)v=2.0f; SaveConfig(); } break;
    case S_ITEMICON_SIZE:if (delta) { float& v=CurTune().itemIconScale; v+=delta*0.05f; if(v<0.10f)v=0.10f; if(v>2.0f)v=2.0f; SaveConfig(); } break;
    case S_MENUPOS: if (delta == 0)   // OK enters the live nudge mode (Left/Right do nothing - not a value)
        { gPosOrigX = gMenuPosX; gPosOrigY = gMenuPosY; gPosMode = true; gUi = UI_BROWSE; }
        break;
    case S_MENUPOSRESET: if (delta == 0) { gMenuPosX = 0.0f; gMenuPosY = 0.0f; SaveConfig(); } break;
    case S_ITEMTITLE_SIZE: if (delta) { float& v=CurTune().itemTitleScale; v+=delta*0.05f; if(v<0.10f)v=0.10f; if(v>2.0f)v=2.0f; CreateMenuFormats(); SaveConfig(); } break;
    case S_ITEMTITLE_SHOW: CurTune().itemTitleShow = !CurTune().itemTitleShow; SaveConfig(); break;
    case S_CATTITLE_SIZE:  if (delta) { float& v=CurTune().catTitleScale; v+=delta*0.05f; if(v<0.10f)v=0.10f; if(v>2.0f)v=2.0f; CreateMenuFormats(); SaveConfig(); } break;
    case S_CATTITLE_SHOW:  CurTune().catTitleShow = !CurTune().catTitleShow; SaveConfig(); break;
    case S_CATTITLE_ALL:   CurTune().catTitleAll = !CurTune().catTitleAll; SaveConfig(); break;
    case S_EDIT:      if (delta == 0) EnterEditMode(); break;
    case S_CLOSE:     if (delta == 0) MoveCategory(-1); break; // leave the Settings tab
    }
}
// --- Settings sections (RetroArch-style submenus) ---
static const wchar_t* SectionLabel(int s)
{
    switch (s) {
    case SEC_APPEAR: return L"Background";
    case SEC_CLOCK:  return L"Clock & date";
    case SEC_SAVER:  return L"Screensaver";
    case SEC_SYSTEM: return L"System";
    case SEC_MENU:   return L"Menu";
    case SEC_EDIT:   return L"Edit";
    }
    return L"";
}
static const wchar_t* SectionDesc(int s)
{
    switch (s) {
    case SEC_APPEAR: return L"Background, ribbon, theme and font.";
    case SEC_CLOCK:  return L"The on-screen clock and date: size, position and format.";
    case SEC_SAVER:  return L"Screensaver: idle timing, dimming and the clock.";
    case SEC_SYSTEM: return L"System behaviour, e.g. keeping the display awake.";
    case SEC_MENU:   return L"Menu layout: orientation, placement, spacing and titles.";
    case SEC_EDIT:   return L"Add, edit, reorder or delete categories and items.";
    }
    return L"";
}
// The SettingRow list that belongs to each section (order = display order in the submenu).
static const int* SectionRows(int s, int* count)
{
    static const int appear[] = { S_BG, S_BGIMAGE, S_RIBBON, S_RIBBONCOL, S_RIBBONCYCLE, S_RIBBONOPACITY, S_RIBBONSHEEN,
                                  S_THEME, S_THEMETOPCOL, S_THEMEBOTCOL, S_UIFONT };
    static const int clock[]  = { S_CLOCKSIZE, S_CLOCKCORNER, S_CLOCKBOLD, S_DATEON, S_DATEBOLD,
                                  S_DATEFMT, S_DATESEP, S_HOURFMT, S_BLINKCOLON };
    static const int saver[]  = { S_SAVER, S_TIMEOUT, S_DIMEN, S_DIM, S_SAVERCLOCK };
    static const int system[] = { S_KEEPAWAKE, S_RETURNHOME, S_AUTOSTART, S_KIOSK };
    static const int menu[]   = { S_ORIENT, S_PLACE, S_ENLARGE, S_SUBDIR, S_LISTANIM, S_ALIGNH, S_ALIGNV,
        S_MENUSCALE, S_CATICON_SIZE, S_ITEMICON_SIZE,
        S_SP_ITEM, S_FOCUSGAP, S_SP_CATITEM, S_SP_CAT, S_SP_ICONTITLE,
        S_ITEMTITLE_SIZE, S_ITEMTITLE_SHOW, S_CATTITLE_SIZE, S_CATTITLE_SHOW, S_CATTITLE_ALL, S_CAT_ICONTITLE,
        S_MENUPOS, S_MENUPOSRESET };
    static const int edit[]   = { S_EDIT };
    switch (s) {
    case SEC_APPEAR: *count = (int)(sizeof(appear) / sizeof(appear[0])); return appear;
    case SEC_CLOCK:  *count = (int)(sizeof(clock)  / sizeof(clock[0]));  return clock;
    case SEC_SAVER:  *count = (int)(sizeof(saver)  / sizeof(saver[0]));  return saver;
    case SEC_SYSTEM: *count = (int)(sizeof(system) / sizeof(system[0])); return system;
    case SEC_MENU:   *count = (int)(sizeof(menu)   / sizeof(menu[0]));   return menu;
    case SEC_EDIT:   *count = (int)(sizeof(edit)   / sizeof(edit[0]));   return edit;
    }
    *count = 0; return nullptr;
}
static int SectionFirstRow(int s)
{
    int cnt = 0; const int* rows = SectionRows(s, &cnt);
    for (int i = 0; i < cnt; ++i) if (!SettingIsHidden(rows[i])) return rows[i];
    return (cnt > 0) ? rows[0] : (int)S_BG;
}
// Move gSettingsSel within the active section (skip hidden rows, wrap).
static int SectionStep(int dir)
{
    int cnt = 0; const int* rows = SectionRows(gSettingsSection, &cnt);
    if (cnt <= 0) return gSettingsSel;
    int idx = 0; for (int i = 0; i < cnt; ++i) if (rows[i] == gSettingsSel) { idx = i; break; }
    for (int k = 0; k < cnt; ++k)
    { idx = (idx + dir + cnt) % cnt; if (!SettingIsHidden(rows[idx])) return rows[idx]; }
    return gSettingsSel;
}
// Settings POPUP navigation (UI_SETTINGS). Depth 0 = the section list (pick a section, OK/Right opens
// it, Cancel/Left closes the popup); depth 1 = inside a section (Up/Down pick a row, Left/Right change
// the value in place, OK toggles/opens, Cancel backs out to the section list). The underlying menu is
// untouched (no category move / list animation) - this is a modal overlay, not a tab.
// --- settings option lists (a row with 3+ named choices is edited via a pop-up picker) ---
// On/Off toggles + numeric sliders stay inline (Left/Right); these are the multi-choice rows.
static bool SettingIsList(int r)
{
    switch (r) {
    case S_BG: case S_RIBBON: case S_THEME: case S_UIFONT:
    case S_ALIGNH: case S_ALIGNV: case S_LISTANIM:
    case S_DATEFMT: case S_DATESEP: case S_HOURFMT: case S_CLOCKCORNER: return true;
    default: return false;
    }
}
static int SettingOptionCount(int r)
{
    switch (r) {
    case S_BG:          return BgChoiceCount();
    case S_RIBBON:      return (int)gRibbonList.size();
    case S_THEME:       return kThemeCount + 1;                 // themes + Custom
    case S_UIFONT:      return kUiFontCount;
    case S_ALIGNH: case S_ALIGNV: return 3;
    case S_LISTANIM:    return 4;
    case S_DATEFMT:     return 13;
    case S_DATESEP:     return 3;
    case S_HOURFMT:     return 4;
    case S_CLOCKCORNER: return 4;
    default:            return 0;
    }
}
static int SettingOptionCurrent(int r)
{
    switch (r) {
    case S_BG:          return gBgChoice;
    case S_RIBBON:      return gRibbonChoice;
    case S_THEME:       return (gTheme >= 0 && gTheme <= kThemeCount) ? gTheme : 0;
    case S_UIFONT:      return (gUiFontIdx >= 0 && gUiFontIdx < kUiFontCount) ? gUiFontIdx : 0;
    case S_ALIGNH:      return CurTune().alignH;
    case S_ALIGNV:      return CurTune().alignV;
    case S_LISTANIM:    return (CurTune().listAnim >= 0 && CurTune().listAnim <= 3) ? CurTune().listAnim : 1;
    case S_DATEFMT:     return gDateFormat;
    case S_DATESEP:     return gDateSep;
    case S_HOURFMT:     return gHourFormat;
    case S_CLOCKCORNER: return gClockCorner;
    default:            return 0;
    }
}
static std::wstring SettingOptionLabel(int r, int i)
{
    switch (r) {
    case S_BG:
        if (i == 0)                             return L"None";
        if (i == BgRibbonChoice())              return L"Ribbon";
        if (i == BgImageChoice())               return L"Image";
        if (i >= 1 && i <= (int)gBgList.size()) return Utf8ToWide(gPipes[gBgList[i - 1]].name);
        return L"-";
    case S_RIBBON:
        return (i >= 0 && i < (int)gRibbonList.size()) ? Utf8ToWide(gPipes[gRibbonList[i]].name) : std::wstring(L"-");
    case S_THEME:
        return (i < kThemeCount) ? Utf8ToWide(kThemes[i].name) : std::wstring(L"Custom");
    case S_UIFONT:
        return (i >= 0 && i < kUiFontCount) ? std::wstring(kUiFonts[i]) : std::wstring(L"-");
    case S_ALIGNH:      return i == 0 ? L"Left" : i == 1 ? L"Center" : L"Right";
    case S_ALIGNV:      return i == 0 ? L"Top"  : i == 1 ? L"Center" : L"Bottom";
    case S_LISTANIM:    return i == 0 ? L"None" : i == 2 ? L"Fade" : i == 3 ? L"Axis slide" : L"Slide";
    case S_DATEFMT:     return DateFormatExample(i);
    case S_DATESEP:     return i == 1 ? L"- (dash)" : i == 2 ? L"/ (slash)" : L". (dot)";
    case S_HOURFMT:     return i == 1 ? L"12-hour (localized)" : i == 2 ? L"24-hour"
                             : i == 3 ? L"12-hour (English)" : L"System format";
    case S_CLOCKCORNER: return i == 0 ? L"Top-left" : i == 1 ? L"Top-right"
                             : i == 2 ? L"Bottom-right" : L"Bottom-left";
    default:            return L"";
    }
}
static void SettingsNav(MenuAction a)
{
    if (gSettingsDepth == 0)                              // section list
    {
        switch (a)
        {
        case ACT_UP:     gSectionSel = (gSectionSel - 1 + SEC_COUNT) % SEC_COUNT; break;
        case ACT_DOWN:   gSectionSel = (gSectionSel + 1) % SEC_COUNT;             break;
        case ACT_OK:
        case ACT_RIGHT:  if (gSectionSel == SEC_EDIT) EnterEditMode();            // Edit: close popup, enter edit mode
                         else { gSettingsSection = gSectionSel; gSettingsDepth = 1;
                                gSettingsSel = SectionFirstRow(gSectionSel); }    break;
        case ACT_CANCEL:
        case ACT_LEFT:   gUi = UI_BROWSE;                                         break;  // close the popup
        default: break;
        }
    }
    else if (gSettingsDepth == 1)                        // inside a section: rows change in place
    {
        switch (a)
        {
        case ACT_UP:     gSettingsSel = SectionStep(-1); break;
        case ACT_DOWN:   gSettingsSel = SectionStep(+1); break;
        case ACT_LEFT:   SettingApply(gSettingsSel, -1); break;   // inline quick-cycle (kept)
        case ACT_RIGHT:  SettingApply(gSettingsSel, +1); break;
        case ACT_OK:     if (SettingIsList(gSettingsSel))         // list row -> open the picker
                         { gSettingsPickSnap = SettingOptionCurrent(gSettingsSel); gSettingsDepth = 2; }
                         else SettingApply(gSettingsSel, 0);  break;  // toggle / action
        case ACT_CANCEL: gSettingsDepth = 0;             break;  // back to the section list
        default: break;
        }
    }
    else                                                 // depth 2: option picker (live-applied)
    {
        switch (a)
        {
        case ACT_UP:   case ACT_LEFT:  SettingApply(gSettingsSel, -1); break;
        case ACT_DOWN: case ACT_RIGHT: SettingApply(gSettingsSel, +1); break;
        case ACT_OK:     gSettingsDepth = 1; break;             // keep the highlighted choice
        case ACT_CANCEL: {                                      // restore the snapshot, then back
            int guard = SettingOptionCount(gSettingsSel) + 1;
            while (SettingOptionCurrent(gSettingsSel) != gSettingsPickSnap && guard-- > 0)
                SettingApply(gSettingsSel, +1);
            gSettingsDepth = 1; break;
        }
        default: break;
        }
    }
}
// Open the Settings popup (S / Tab / Start / gamepad Back). Starts at the section list.
static void OpenSettings()
{
    LeaveEditMode();
    gSettingsDepth = 0; gSectionSel = 0;
    gSettingsSection = SEC_APPEAR; gSettingsSel = SectionFirstRow(SEC_APPEAR);
    gUi = UI_SETTINGS;
    gSettingsHintMs = 0.0f;        // the user found Settings - stop showing the startup hint
}

// Route a menu action to whatever UI mode is active.
static void Dispatch(MenuAction a)
{
    switch (gUi)
    {
    case UI_BROWSE:   ApplyMenuAction(a); break;   // ACT_OK -> LaunchSelected (see ApplyMenuAction)
    case UI_SETTINGS: SettingsNav(a);     break;
    case UI_ACTION:   ActionNav(a);       break;
    case UI_KEYBOARD: KbNav(a); if (a == ACT_CANCEL) KbCancelEdit(); else if (a == ACT_OK) KbActivate(); break;
    case UI_FILES:    FilesNav(a);        break;
    case UI_COLOR:    ColorNav(a);         break;
    }
}

// --- edit-mode overlay drawing (its own D2D batch, on top of the menu) ---
static void DrawEditOverlay(const MenuLayout& L)
{
    if (gUi == UI_BROWSE || !gD2DCtx || !gD2DTarget || !gItemFmt) return;
    const float so = L.shadowOffset;
    const float ov = gMenuAlpha;         // fade the whole overlay with the menu (out on screensaver)
    gD2DCtx->BeginDraw();

    // LIVE PREVIEW: while adjusting the Menu-layout section, dock the panel to the side and use only a
    // light dim so the menu behind stays visible and updates as the values change. Other settings /
    // the section list stay a centred modal with a heavy dim.
    const bool liveMenuPreview = (gUi == UI_SETTINGS && gSettingsDepth >= 1 && gSettingsSection == SEC_MENU);
    // Appearance section: the user is changing the background / ribbon / theme, so show it with NO dim
    // (the ribbon must read exactly as it will look) and dock the panel to the side so more is visible.
    const bool liveAppearance  = (gUi == UI_SETTINGS && gSettingsDepth >= 1 && gSettingsSection == SEC_APPEAR);
    const bool sideDock        = liveMenuPreview || liveAppearance;
    // dim the whole screen so the overlay reads (fades with the menu). The edit context popup is small
    // and anchored to its item, so it only needs a light dim; the full-screen modes dim more. The colour
    // picker AND the Appearance section draw NO full-screen dim - the ribbon/theme behind must be seen
    // exactly as it will look; only the panel itself sits on top.
    if (gUi != UI_COLOR && !liveAppearance)
    {
        gShadow->SetOpacity((gUi == UI_ACTION ? 0.42f : liveMenuPreview ? 0.30f : 0.92f) * ov);
        gD2DCtx->FillRectangle(D2D1::RectF(0, 0, (float)gW, (float)gH), gShadow);
    }

    auto drawText = [&](IDWriteTextFormat* fmt, const std::wstring& s, float x, float y, float w, float h, float op)
    {
        if (!fmt) return;
        gShadow->SetOpacity(0.7f * op * ov);
        gD2DCtx->DrawText(s.c_str(), (UINT32)s.size(), fmt, D2D1::RectF(x + so, y + so, x + w + so, y + h + so), gShadow);
        gBrush->SetOpacity(op * ov);
        gD2DCtx->DrawText(s.c_str(), (UINT32)s.size(), fmt, D2D1::RectF(x, y, x + w, y + h), gBrush);
    };
    gItemFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    gItemFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    const float pad = 60.0f * L.scale;
    const float lineH = L.fontSize * 1.5f;
    float x = pad, y = pad;

    if (gUi == UI_SETTINGS)
    {
        // Solid panel (same style as the file browser / keyboard popups). Centred normally; docked to
        // the RIGHT while live-previewing the Menu section so the menu stays visible on the left.
        float panelW = (float)gW * (sideDock ? 0.42f : 0.52f);
        float minW = 560.0f * L.scale; if (panelW < minW) panelW = minW;
        if (panelW > (float)gW - 2 * pad) panelW = (float)gW - 2 * pad;
        float panelH = (float)gH * 0.72f;
        float maxH = (float)gH - 2 * pad; if (panelH > maxH) panelH = maxH;
        // Docked side (live preview): keep the menu visible by docking OPPOSITE a right-placed vertical menu
        // column; otherwise dock right (default). Non-preview panels stay centred.
        float px;
        if (sideDock)
        {
            float colX = 48.0f * L.scale + L.iconSize * 0.5f;      // matches DrawMenu catColX
            if (CurTune().subReverse) colX = (float)gW - colX;     // reversed: column mirrors to the right
            colX += gMenuPosX;                                     // whole-menu screen offset
            bool menuRight = gVertical && colX > (float)gW * 0.5f;
            px = menuRight ? pad : ((float)gW - panelW - pad);     // menu on right -> dock LEFT, else dock right
        }
        else px = ((float)gW - panelW) * 0.5f;
        float py = ((float)gH - panelH) * 0.5f;
        D2D1_ROUNDED_RECT rr = { D2D1::RectF(px, py, px + panelW, py + panelH), 14.0f * L.scale, 14.0f * L.scale };
        gShadow->SetOpacity(0.97f * ov); gD2DCtx->FillRoundedRectangle(rr, gShadow);
        gBrush->SetOpacity(0.5f * ov);   gD2DCtx->DrawRoundedRectangle(rr, gBrush, 1.5f * L.scale);
        gBrush->SetOpacity(1.0f);

        const float ipad     = 34.0f * L.scale;
        const float ix       = px + ipad;
        const float contentW = panelW - 2 * ipad;
        const float indent   = L.fontSize * 0.8f;
        const float hintY    = py + panelH - ipad - lineH;
        const float descY    = hintY - lineH * 1.15f;      // one-line description above the hint line
        const float titleY   = py + ipad;

        if (gSettingsDepth == 0)
        {
            // Section list: the top level of the popup.
            drawText(gCatFmt, L"Settings", ix, titleY, contentW, L.fontSize * 2.0f, 1.0f);
            float ry = titleY + L.fontSize * 2.4f;
            for (int s = 0; s < (int)SEC_COUNT; ++s)
            {
                bool sel = (s == gSectionSel);
                float op = sel ? 1.0f : 0.55f;
                std::wstring lab = (sel ? L"> " : L"   ") + std::wstring(SectionLabel(s));
                drawText(gItemFmt, lab, ix + indent, ry, contentW, lineH, op);
                ry += lineH;
            }
            drawText(gSubFmt, SectionDesc(gSectionSel), ix, descY, contentW, lineH, 0.85f);
            drawText(gSubFmt, L"Up/Down select   A/Enter open   B/Esc close",
                     ix, hintY, contentW, lineH, 0.7f);
        }
        else if (gSettingsDepth == 1)
        {
            // Inside a section: its (visible) rows, scroll-windowed, value on the right.
            drawText(gCatFmt, SectionLabel(gSettingsSection), ix, titleY, contentW, L.fontSize * 2.0f, 1.0f);
            const float listTop = titleY + L.fontSize * 2.4f;
            const float listBot = descY - lineH * 0.4f;
            int maxVis = (int)((listBot - listTop) / lineH);
            if (maxVis < 3) maxVis = 3;
            int cnt = 0; const int* rows = SectionRows(gSettingsSection, &cnt);
            std::vector<int> vis;
            for (int i = 0; i < cnt; ++i) if (!SettingIsHidden(rows[i])) vis.push_back(rows[i]);
            int selIdx = 0;
            for (int i = 0; i < (int)vis.size(); ++i) if (vis[i] == gSettingsSel) { selIdx = i; break; }
            static int scroll = 0;
            if (selIdx < scroll)                       scroll = selIdx;
            if (selIdx > scroll + maxVis - 1)          scroll = selIdx - maxVis + 1;
            if (scroll > (int)vis.size() - maxVis)     scroll = (int)vis.size() - maxVis;
            if (scroll < 0)                            scroll = 0;
            float ry = listTop;
            for (int i = scroll; i < (int)vis.size() && i < scroll + maxVis; ++i)
            {
                int r = vis[i];
                bool selrow = (r == gSettingsSel);
                float op = selrow ? 1.0f : 0.55f;
                std::wstring lab = (selrow ? L"> " : L"   ") + std::wstring(SettingLabel(r));
                drawText(gItemFmt, lab, ix + indent, ry, contentW, lineH, op);
                std::wstring val = SettingValue(r);
                if (!val.empty())
                {
                    if (selrow) val = L"< " + val + L" >";    // hint that Left/Right changes it
                    gItemFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                    gShadow->SetOpacity(0.7f * op * ov);
                    gD2DCtx->DrawText(val.c_str(), (UINT32)val.size(), gItemFmt,
                                      D2D1::RectF(ix + so, ry + so, ix + contentW + so, ry + lineH + so), gShadow);
                    gBrush->SetOpacity(op * ov);
                    gD2DCtx->DrawText(val.c_str(), (UINT32)val.size(), gItemFmt,
                                      D2D1::RectF(ix, ry, ix + contentW, ry + lineH), gBrush);
                    gItemFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }
                ry += lineH;
            }
            drawText(gSubFmt, SettingDesc(gSettingsSel), ix, descY, contentW, lineH, 0.85f);
            drawText(gSubFmt, L"Up/Down select   Left/Right change   A/Enter toggle or open   B/Esc back",
                     ix, hintY, contentW, lineH, 0.7f);
        }
        else
        {
            // Option picker (depth 2): the full list of the highlighted setting's choices. The current
            // one is marked and applied live (so appearance changes preview); OK keeps it, B/Esc restores.
            drawText(gCatFmt, SettingLabel(gSettingsSel), ix, titleY, contentW, L.fontSize * 2.0f, 1.0f);
            const float listTop = titleY + L.fontSize * 2.4f;
            const float listBot = descY - lineH * 0.4f;
            int maxVis = (int)((listBot - listTop) / lineH);
            if (maxVis < 3) maxVis = 3;
            int n = SettingOptionCount(gSettingsSel);
            int cur = SettingOptionCurrent(gSettingsSel);
            if (cur < 0) cur = 0; if (cur > n - 1) cur = n - 1;
            static int pscroll = 0;
            if (cur < pscroll)                pscroll = cur;
            if (cur > pscroll + maxVis - 1)   pscroll = cur - maxVis + 1;
            if (pscroll > n - maxVis)         pscroll = n - maxVis;
            if (pscroll < 0)                  pscroll = 0;
            float ry = listTop;
            for (int i = pscroll; i < n && i < pscroll + maxVis; ++i)
            {
                bool selrow = (i == cur);
                float op = selrow ? 1.0f : 0.55f;
                std::wstring lab = (selrow ? L"> " : L"   ") + SettingOptionLabel(gSettingsSel, i);
                drawText(gItemFmt, lab, ix + indent, ry, contentW, lineH, op);
                ry += lineH;
            }
            // Scroll affordance: a triangle on the first/last visible row when the list runs off the
            // window, so the user knows more options are above / below. (\u escapes keep the source ASCII.)
            gItemFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            auto arrow = [&](const wchar_t* g, float ay)
            {
                gShadow->SetOpacity(0.7f * ov);
                gD2DCtx->DrawText(g, 1, gItemFmt, D2D1::RectF(ix + so, ay + so, ix + contentW + so, ay + lineH + so), gShadow);
                gBrush->SetOpacity(0.9f * ov);
                gD2DCtx->DrawText(g, 1, gItemFmt, D2D1::RectF(ix, ay, ix + contentW, ay + lineH), gBrush);
            };
            if (pscroll > 0)              arrow(L"\u25B2", listTop);
            if (pscroll + maxVis < n)     arrow(L"\u25BC", listTop + (float)(maxVis - 1) * lineH);
            gItemFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            gBrush->SetOpacity(1.0f);
            drawText(gSubFmt, SettingDesc(gSettingsSel), ix, descY, contentW, lineH, 0.85f);
            drawText(gSubFmt, L"Up/Down choose   A/Enter select   B/Esc cancel",
                     ix, hintY, contentW, lineH, 0.7f);
        }
    }
    else if (gUi == UI_ACTION)
    {
        // Compact context popup ANCHORED beside the focused item/category (not a full-screen menu), so
        // the menu stays visible behind it. The header names what is being edited. Two pages: the
        // context list and the field editor.
        MenuCategory* c = CurCat(); MenuItem* it = CurItem();
        // "catCtx" = this page reads its title/icon from the CATEGORY (a container OR a top-level item),
        // not from a MenuItem. Top-level-item field/context pages store their data on the tab.
        bool catCtx = (gActPage == AP_CAT_FIELDS) || (gActPage == AP_TOPITEM_FIELDS)
                    || (gActPage == AP_CTX && gEditFocusCat);
        std::wstring hdr = (gActPage == AP_ADDCHOICE) ? std::wstring(L"Add to menu")
                         : catCtx ? (c ? c->title : std::wstring(L"Category"))
                                  : (it ? it->title : std::wstring(L"Item"));
        const wchar_t* const* list = ActList();
        int n = ActCount();

        const float ipad  = 18.0f * L.scale;
        const float rowH  = lineH;
        const float headH = L.fontSize * 1.6f;
        float panelW = (float)gW * 0.34f; if (panelW < 380.0f * L.scale) panelW = 380.0f * L.scale;
        float panelH = headH + n * rowH + ipad * 2.0f;

        // Anchor the popup BESIDE the focused element (to its right; flip to its left if that would run off
        // the right edge), then clamp so it always stays fully on screen (both orientations).
        float bx = gEditAnchorX + gEditAnchorHalf + 16.0f * L.scale;
        if (bx + panelW > (float)gW - 20.0f * L.scale)
            bx = gEditAnchorX - gEditAnchorHalf - 16.0f * L.scale - panelW;   // no room on the right -> place on the left
        if (bx < 20.0f * L.scale) bx = 20.0f * L.scale;
        if (bx + panelW > (float)gW - 20.0f * L.scale) bx = (float)gW - 20.0f * L.scale - panelW;  // keep on screen both ends
        float by = gEditAnchorY - headH * 0.5f;
        if (by + panelH > (float)gH - 20.0f * L.scale) by = (float)gH - 20.0f * L.scale - panelH;
        if (by < 20.0f * L.scale) by = 20.0f * L.scale;

        // Panel background: dark translucent rounded rect + a faint outline.
        D2D1_ROUNDED_RECT rr = { D2D1::RectF(bx, by, bx + panelW, by + panelH), 12.0f * L.scale, 12.0f * L.scale };
        gShadow->SetOpacity(0.97f * ov);
        gD2DCtx->FillRoundedRectangle(rr, gShadow);
        gBrush->SetOpacity(0.5f * ov);
        gD2DCtx->DrawRoundedRectangle(rr, gBrush, 1.5f * L.scale);

        float tx = bx + ipad, tw = panelW - ipad * 2.0f, ty = by + ipad * 0.6f;
        drawText(gSubFmt, hdr, tx, ty, tw, headH, 0.8f);
        // On a field page, show the icon that will be used (auto-populated from the picked file), so the
        // user sees it before changing it via Icon (browse).
        if (gActPage == AP_ITEM_FIELDS || gActPage == AP_CAT_FIELDS || gActPage == AP_TOPITEM_FIELDS)
        {
            std::wstring iconName = catCtx ? (c ? c->icon : std::wstring()) : (it ? it->icon : std::wstring());
            if (!iconName.empty())
            {
                float ps = headH * 1.3f;
                DrawIcon(LoadIcon(iconName.c_str()), bx + panelW - ipad - ps * 0.5f, ty + headH * 0.45f, ps, ov);
            }
        }
        ty += headH;
        for (int i = 0; i < n; ++i)
        {
            bool selrow = (i == gActionSel);
            std::wstring label = list[i];
            if (list[i] == kRowTrackWindow)   // toggle row: append its current On/Off state
            {
                bool on = (gActPage == AP_TOPITEM_FIELDS) ? (c && c->trackWindow) : (it && it->trackWindow);
                label += on ? L":  On" : L":  Off";
            }
            std::wstring t = (selrow ? L"> " : L"   ") + label;
            drawText(gItemFmt, t, tx, ty, tw, rowH, selrow ? 1.0f : 0.65f);
            ty += rowH;
        }
    }
    else if (gUi == UI_KEYBOARD)
    {
        // Centred on screen inside a solid rounded panel (was a transparent left-aligned block over the
        // menu). Each row is centred individually.
        const float keyW = 64.0f * L.scale, keyH = 56.0f * L.scale, gap = 8.0f * L.scale;
        const float cx = (float)gW * 0.5f;
        float rowW = 10.0f * (keyW + gap) - gap;                 // widest character row (10 keys)
        float specW = 0.0f;
        for (int s = 0; s < (int)KS_COUNT; ++s)
        { std::wstring lab = kKbSpecials[s]; if (s == KS_SHIFT && gKbUpper) lab = L"SHIFT";
          specW += (float)lab.size() * L.fontSize * 0.7f + 24.0f * L.scale + gap; }
        specW -= gap;
        float blockW = rowW > specW ? rowW : specW;
        float blockH = L.fontSize * 1.9f + lineH * 1.4f + 4.0f * (keyH + gap) + keyH + lineH * 1.6f;
        float panelW = blockW + 80.0f * L.scale, panelH = blockH + 60.0f * L.scale;
        float px = cx - panelW * 0.5f, py = ((float)gH - panelH) * 0.5f;
        D2D1_ROUNDED_RECT rr = { D2D1::RectF(px, py, px + panelW, py + panelH), 14.0f * L.scale, 14.0f * L.scale };
        gShadow->SetOpacity(0.97f * ov); gD2DCtx->FillRoundedRectangle(rr, gShadow);
        gBrush->SetOpacity(0.5f * ov);   gD2DCtx->DrawRoundedRectangle(rr, gBrush, 1.5f * L.scale);

        float ky = py + 30.0f * L.scale;
        drawText(gCatFmt, gKbPrompt, cx - blockW * 0.5f, ky, blockW, L.fontSize * 1.6f, 1.0f);
        ky += L.fontSize * 1.9f;
        drawText(gItemFmt, gKbText, cx - blockW * 0.5f, ky, blockW, lineH, 1.0f);
        ky += lineH * 1.4f;
        for (int r = 0; r < 4; ++r)
        {
            const wchar_t* row = kKbRows[r];
            int len = (int)wcslen(row);
            float rw = (float)len * (keyW + gap) - gap;
            float kx0 = cx - rw * 0.5f;
            for (int col = 0; col < len; ++col)
            {
                wchar_t ch = row[col];
                if (gKbUpper && ch >= L'a' && ch <= L'z') ch = (wchar_t)(ch - L'a' + L'A');
                bool selk = (gKbRow == r && gKbCol == col);
                float kx = kx0 + col * (keyW + gap);
                if (selk) { gBrush->SetOpacity(0.22f * ov); gD2DCtx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(kx, ky, kx + keyW, ky + keyH), 6.0f * L.scale, 6.0f * L.scale), gBrush); }
                std::wstring ks(1, ch);
                drawText(gItemFmt, ks, kx + keyW * 0.3f, ky + keyH * 0.12f, keyW, keyH, selk ? 1.0f : 0.7f);
            }
            ky += keyH + gap;
        }
        float sx = cx - specW * 0.5f;
        for (int s = 0; s < (int)KS_COUNT; ++s)
        {
            bool selk = (gKbRow == 4 && gKbCol == s);
            std::wstring lab = kKbSpecials[s];
            if (s == KS_SHIFT && gKbUpper) lab = L"SHIFT";
            float w = (float)lab.size() * L.fontSize * 0.7f + 24.0f * L.scale;
            if (selk) { gBrush->SetOpacity(0.22f * ov); gD2DCtx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sx, ky, sx + w, ky + keyH), 6.0f * L.scale, 6.0f * L.scale), gBrush); }
            drawText(gItemFmt, lab, sx + 10.0f * L.scale, ky + keyH * 0.12f, w, keyH, selk ? 1.0f : 0.7f);
            sx += w + gap;
        }
        ky += keyH + gap;
        drawText(gSubFmt, L"D-pad move   A select key   OK confirms   B cancels   (or just type)",
                 cx - blockW * 0.5f, ky, blockW, lineH, 0.7f);
    }
    else if (gUi == UI_FILES)
    {
        // Centred solid panel. When picking an image, the right side shows a live preview of the
        // highlighted file.
        bool preview = gBrowseImagesOnly;
        float panelW = (float)gW * (preview ? 0.68f : 0.60f), panelH = (float)gH * 0.74f;
        float px = ((float)gW - panelW) * 0.5f, py = ((float)gH - panelH) * 0.5f;
        D2D1_ROUNDED_RECT rr = { D2D1::RectF(px, py, px + panelW, py + panelH), 14.0f * L.scale, 14.0f * L.scale };
        gShadow->SetOpacity(0.97f * ov); gD2DCtx->FillRoundedRectangle(rr, gShadow);
        gBrush->SetOpacity(0.5f * ov);   gD2DCtx->DrawRoundedRectangle(rr, gBrush, 1.5f * L.scale);

        float ix = px + 30.0f * L.scale, contentW = panelW - 60.0f * L.scale, iy = py + 24.0f * L.scale;
        drawText(gCatFmt, (preview ? L"Pick an image" : L"Pick a file"), ix, iy, contentW, L.fontSize * 1.6f, 1.0f);
        iy += L.fontSize * 1.9f;
        drawText(gSubFmt, gBrowsePath.empty() ? std::wstring(L"(drives)") : gBrowsePath, ix, iy, contentW, lineH, 0.7f);
        iy += lineH * 1.2f;
        float listTop = iy;
        float listBot = py + panelH - lineH * 1.6f;
        float listW   = preview ? contentW * 0.56f : contentW;   // leave room for the preview pane
        int maxRows = (int)((listBot - iy) / lineH); if (maxRows < 1) maxRows = 1;
        int n = (int)gBrowseEntries.size();
        int first = 0; if (gBrowseSel >= maxRows) first = gBrowseSel - maxRows + 1;
        // Long file names previously WRAPPED onto a second line and overlapped the next row. Keep each
        // entry on ONE line: no word-wrap + an ellipsis trim so any overflow becomes a trailing "...".
        IDWriteInlineObject* ell = nullptr;
        if (gDWrite) gDWrite->CreateEllipsisTrimmingSign(gItemFmt, &ell);
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        gItemFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        gItemFmt->SetTrimming(&trim, ell);
        for (int i = first; i < n && i < first + maxRows; ++i)
        {
            bool selrow = (i == gBrowseSel);
            std::wstring t = (selrow ? L"> " : L"   ") + gBrowseEntries[i].name + (gBrowseEntries[i].isDir ? L"\\" : L"");
            drawText(gItemFmt, t, ix, iy, listW, lineH, selrow ? 1.0f : 0.6f);
            iy += lineH;
        }
        gItemFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);          // restore (gItemFmt is shared)
        DWRITE_TRIMMING trimOff = { DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };
        gItemFmt->SetTrimming(&trimOff, nullptr);
        if (ell) ell->Release();
        if (preview)                                             // preview pane on the right
        {
            float bxr = ix + listW + 24.0f * L.scale;
            float bw  = (px + panelW - 30.0f * L.scale) - bxr;
            float bh  = listBot - listTop;
            D2D1_ROUNDED_RECT pr = { D2D1::RectF(bxr, listTop, bxr + bw, listTop + bh), 8.0f * L.scale, 8.0f * L.scale };
            gBrush->SetOpacity(0.18f * ov); gD2DCtx->DrawRoundedRectangle(pr, gBrush, 1.2f * L.scale);
            if (gBrowseSel >= 0 && gBrowseSel < n && !gBrowseEntries[gBrowseSel].isDir)
            {
                std::wstring full = PathJoin(gBrowsePath, gBrowseEntries[gBrowseSel].name);
                IconTex t = LoadIcon(full.c_str());
                if (t.bmp)
                {
                    float sz = (bw < bh ? bw : bh) * 0.86f;
                    DrawIcon(t, bxr + bw * 0.5f, listTop + bh * 0.5f, sz, ov);
                }
                else drawText(gSubFmt, L"(no preview)", bxr, listTop + bh * 0.5f, bw, lineH, 0.5f);
            }
        }
        drawText(gSubFmt, L"Up/Down move   A open/select   B up a level / close",
                 ix, py + panelH - lineH * 1.3f, contentW, lineH, 0.7f);
    }
    else if (gUi == UI_COLOR)
    {
        // HSV colour map: a hue bar (left) + a saturation (x) x value (y) square. The panel is sized to
        // the CONTENT (square as big as the height budget allows) so there is no dead space on the right.
        const float hueW = 44.0f * L.scale, gapx = 22.0f * L.scale, ipad = 34.0f * L.scale;
        const float titleH = L.fontSize * 2.2f, swatchH = L.fontSize * 1.5f;
        const float belowSq = L.fontSize * 0.7f + swatchH + L.fontSize * 0.9f + L.fontSize * 1.4f; // gap+swatch+gap+hint
        float panelH = (float)gH * 0.66f;
        float sq = panelH - ipad - titleH - belowSq - ipad;                 // square = the leftover height
        if (sq < 120.0f * L.scale) { sq = 120.0f * L.scale; panelH = ipad + titleH + sq + belowSq + ipad; }
        float panelW = ipad + hueW + gapx + sq + ipad;
        float minW = (float)gW * 0.44f; if (panelW < minW) panelW = minW;
        float px = ((float)gW - panelW) * 0.5f, py = ((float)gH - panelH) * 0.5f;

        D2D1_ROUNDED_RECT rr = { D2D1::RectF(px, py, px + panelW, py + panelH), 14.0f * L.scale, 14.0f * L.scale };
        gShadow->SetOpacity(0.9f * ov); gD2DCtx->FillRoundedRectangle(rr, gShadow);
        gBrush->SetOpacity(0.5f * ov);  gD2DCtx->DrawRoundedRectangle(rr, gBrush, 1.5f * L.scale);

        float ix = px + ipad, y = py + ipad;
        const wchar_t* ttl = (gColorTarget == COL_RIBBON)   ? L"Ribbon colour"
                           : (gColorTarget == COL_THEMETOP) ? L"Custom theme - top colour"
                                                            : L"Custom theme - bottom colour";
        drawText(gCatFmt, ttl, ix, y, panelW - 2 * ipad, L.fontSize * 1.6f, 1.0f);
        y += titleH;
        float hueX = ix, sqX = ix + hueW + gapx, top = y;

        // hue bar (top = 0 deg, bottom = 360 deg)
        const int HM = 72;
        for (int i = 0; i < HM; ++i)
        {
            float r,g,b; HsvToRgb((float)i / (float)HM * 360.0f, 1.0f, 1.0f, r,g,b);
            gBrush->SetColor(D2D1::ColorF(r,g,b)); gBrush->SetOpacity(ov);
            gD2DCtx->FillRectangle(D2D1::RectF(hueX, top + sq * (float)i / HM, hueX + hueW, top + sq * (float)(i+1) / HM + 0.5f), gBrush);
        }
        // SV square (x = saturation 0..1, y = value 1..0) for the current hue
        const int N = 48;
        for (int yc = 0; yc < N; ++yc)
            for (int xc = 0; xc < N; ++xc)
            {
                float r,g,b; HsvToRgb(gColH, (float)xc / (N - 1), 1.0f - (float)yc / (N - 1), r,g,b);
                gBrush->SetColor(D2D1::ColorF(r,g,b)); gBrush->SetOpacity(ov);
                gD2DCtx->FillRectangle(D2D1::RectF(sqX + sq * (float)xc / N, top + sq * (float)yc / N,
                                                   sqX + sq * (float)(xc+1) / N + 0.5f, top + sq * (float)(yc+1) / N + 0.5f), gBrush);
            }
        gBrush->SetColor(D2D1::ColorF(1,1,1)); gBrush->SetOpacity(ov);

        // hue marker + SV crosshair (white ring over a dark ring so it reads on any colour)
        float hy = top + sq * (gColH / 360.0f);
        gShadow->SetOpacity(0.8f * ov);
        gD2DCtx->DrawRectangle(D2D1::RectF(hueX - 3*L.scale, hy - 2.5f*L.scale, hueX + hueW + 3*L.scale, hy + 2.5f*L.scale), gShadow, 3.5f*L.scale);
        gD2DCtx->DrawRectangle(D2D1::RectF(hueX - 3*L.scale, hy - 2.5f*L.scale, hueX + hueW + 3*L.scale, hy + 2.5f*L.scale), gBrush, 1.5f*L.scale);
        float cxs = sqX + sq * gColS, cys = top + sq * (1.0f - gColV), rC = 8.0f*L.scale;
        gD2DCtx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cxs, cys), rC, rC), gShadow, 3.5f*L.scale);
        gD2DCtx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cxs, cys), rC, rC), gBrush, 1.5f*L.scale);

        // focus outline on the active zone
        gBrush->SetOpacity(0.95f * ov);
        if (gColFocus == 1)
            gD2DCtx->DrawRectangle(D2D1::RectF(hueX - 4*L.scale, top - 4*L.scale, hueX + hueW + 4*L.scale, top + sq + 4*L.scale), gBrush, 2.5f*L.scale);
        else
            gD2DCtx->DrawRectangle(D2D1::RectF(sqX - 4*L.scale, top - 4*L.scale, sqX + sq + 4*L.scale, top + sq + 4*L.scale), gBrush, 2.5f*L.scale);

        // preview swatch + hex value, below the square (clear of the hint line)
        float r,g,b; HsvToRgb(gColH, gColS, gColV, r,g,b);
        float swY = top + sq + L.fontSize * 0.7f, swW = sq * 0.5f;
        gBrush->SetColor(D2D1::ColorF(r,g,b)); gBrush->SetOpacity(ov);
        D2D1_ROUNDED_RECT swr = { D2D1::RectF(sqX, swY, sqX + swW, swY + swatchH), 6*L.scale, 6*L.scale };
        gD2DCtx->FillRoundedRectangle(swr, gBrush);
        gBrush->SetColor(D2D1::ColorF(1,1,1)); gBrush->SetOpacity(1.0f);
        wchar_t hx[16]; swprintf(hx, 16, L"#%02X%02X%02X", Ch255(r), Ch255(g), Ch255(b));
        drawText(gItemFmt, hx, sqX + swW + 16.0f*L.scale, swY, sq * 0.5f, swatchH, 1.0f);

        drawText(gSubFmt, L"Hue bar: Up/Down   Right -> square (saturation / brightness)   OK save   B cancel",
                 ix, py + panelH - ipad - L.fontSize * 1.2f, panelW - 2 * ipad, L.fontSize * 1.4f, 0.7f);
    }

    gBrush->SetColor(D2D1::ColorF(1,1,1));   // restore (the colour picker recolours the solid brush)
    gBrush->SetOpacity(1.0f);
    gShadow->SetOpacity(0.6f);
    gD2DCtx->EndDraw();
}