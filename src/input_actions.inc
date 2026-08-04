// --- input -> menu actions ---
enum MenuAction { ACT_NONE, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_OK, ACT_CANCEL };
static void EditNav(MenuAction a);            // fwd: edit-mode navigation (focus, move mode)

static void LaunchSelected()
{
    if (gMenu.empty()) return;
    MenuCategory& c = gMenu[gCatSel];
    if (c.isItem)                                    // top-level direct-launch tab: run its own target
    {
        MenuItem tmp; tmp.title = c.title; tmp.icon = c.icon; tmp.target = c.target; tmp.arguments = c.arguments;
        tmp.waitForExit = c.waitForExit;
        tmp.trackWindow = c.trackWindow; tmp.processName = c.processName;
        if (!tmp.target.empty()) LaunchItem(tmp);
        return;
    }
    const std::vector<MenuItem>& items = c.items;
    int sel = gItemSel[gCatSel];
    if (sel < 0 || sel >= (int)items.size()) return;
    LaunchItem(items[sel]);
}

static void MoveItem(int d)
{
    int n = (int)gMenu[gCatSel].items.size();
    if (n <= 0) return;
    int ns = ((gItemSel[gCatSel] + d) % n + n) % n;   // wrap around both ends
    if (ns == gItemSel[gCatSel]) return;
    gItemSel[gCatSel] = ns;
    RetargetItems(false);       // items slide to new positions
}
static void MoveCategory(int d)
{
    int n = (int)gMenu.size();
    if (n <= 0) return;
    int ns = ((gCatSel + d) % n + n) % n;             // wrap around both ends
    if (ns == gCatSel) return;
    // Did this move wrap past an edge (last -> first or first -> last)? If so snap the scroll
    // tween instead of sliding it the whole way back across every category.
    bool wrapped = (d > 0 && ns < gCatSel) || (d < 0 && ns > gCatSel);
    gCatSel = ns;
    gSettingsDepth = 0;                               // moving tabs resets the Settings submenu depth
    if (wrapped) gCatTween.set((float)gCatSel);
    else         gCatTween.retarget((float)gCatSel, XMB_DELAY);
    // Fixed placement: snap the item slots to the new list but GLIDE the category highlight + the
    // item block over to the new category (snapCats = false).
    RetargetItems(true, !gFixedLayout || wrapped);
    // Fixed placement: the category selector glides to the new column. "Slide" is happy to ride that
    // glide, so animate it immediately; None / Fade / Axis slide instead HIDE the list and wait for the
    // glide to settle, then play at the settled column (UpdateListSwitch). Scrolling (pinned item column)
    // and wrap-jumps (selector snaps, no glide) always animate immediately.
    if (gFixedLayout && !wrapped && CurTune().listAnim != 1)
        { gListFade.set(0.0f); gListSwitchDir = d; gListSwitchDelayMs = XMB_DELAY; }
    else
        { gListSwitchDelayMs = -1.0f; StartListSwitch(d); }
}

// Directions transpose with orientation ("follow the axes"): horizontal -> Up/Down = item,
// Left/Right = category; vertical -> Up/Down = category, Left/Right = item.
static void ApplyMenuAction(MenuAction a)
{
    if (gMenu.empty()) return;
    if (gPosMode)                                 // Part C: live "Menu position..." nudge mode (whole menu)
    {
        const float step = 10.0f;                 // px per nudge (auto-repeats when the direction is held)
        switch (a)
        {
        case ACT_LEFT:   gMenuPosX -= step; break;
        case ACT_RIGHT:  gMenuPosX += step; break;
        case ACT_UP:     gMenuPosY -= step; break;
        case ACT_DOWN:   gMenuPosY += step; break;
        // Return to the Settings popup (Menu section, on the "Menu position..." row it was launched from -
        // the section/row state is preserved while nudging), so the user lands back where they started.
        case ACT_OK:     gPosMode = false; SaveConfig(); gUi = UI_SETTINGS; break;                       // confirm + save
        case ACT_CANCEL: gMenuPosX = gPosOrigX; gMenuPosY = gPosOrigY; gPosMode = false; gUi = UI_SETTINGS; break;  // restore
        default: break;
        }
        return;
    }
    if (gEditMode) { EditNav(a); return; }        // edit mode has its own focus / move-mode nav
    // Part C2: a reversed submenu flows the other way (up / left), so the item-axis input inverts too -
    // pressing the direction the cursor should visually move picks the right item. id = +1 normally, -1
    // when the active category is reversed. (Categories are never reversed, so the category axis is stock.)
    const bool cRev = CurTune().subReverse;
    const int  id   = cRev ? -1 : 1;
    switch (a)
    {
    case ACT_UP:    gVertical ? MoveCategory(-1)  : MoveItem(-id);    break;
    case ACT_DOWN:  gVertical ? MoveCategory(+1)  : MoveItem(+id);    break;
    case ACT_LEFT:  gVertical ? MoveItem(-id)     : MoveCategory(-1); break;
    case ACT_RIGHT: gVertical ? MoveItem(+id)     : MoveCategory(+1); break;
    case ACT_OK:    LaunchSelected(); break;
    case ACT_CANCEL: break;
    default: break;
    }
}

// Draw a date + time block (two lines: big time over a smaller date). pos picks which of the four
// screen corners to anchor to (menu clock = top-right; the screensaver clock cycles corners). alpha
// fades the whole block; 0 skips it. Size scales by gClockScale. Time/date come from the OS in the
// user's locale, so 12/24h prefs and locale month/day names (incl. accents) are respected.
// Format a date into buf using one of the presets (fmt = date-format index 0-12) and the numeric
// separator (sep: 0 "." / 1 "-" / 2 "/"). Shared by the live clock and the settings option picker.
static void FormatDateStr(const SYSTEMTIME& st, int fmt, int sep, wchar_t* buf, int cap)
{
    switch (fmt)
    {
    // Grouped: localised (0-4), then English (5-9), then numeric (10-12). Month is always FULL (MMMM).
    case 0:  GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"d MMMM", buf, cap, nullptr); break;           // 29 July (localised)
    case 1:  GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"ddd d MMMM", buf, cap, nullptr); break;       // Mon 29 July (localised)
    case 2:  GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"d MMMM yyyy", buf, cap, nullptr); break;      // 29 July 2024 (localised)
    case 3:  GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"ddd, d MMMM yyyy", buf, cap, nullptr); break; // Mon, 29 July 2024 (localised)
    case 4:  GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, buf, cap, nullptr); break; // Monday, 29 July 2024 (localised)
    case 5:  GetDateFormatEx(L"en-US", 0, &st, L"d MMMM", buf, cap, nullptr); break;                           // 29 July (English)
    case 6:  GetDateFormatEx(L"en-US", 0, &st, L"ddd d MMMM", buf, cap, nullptr); break;                       // Mon 29 July (English)
    case 7:  GetDateFormatEx(L"en-US", 0, &st, L"d MMMM yyyy", buf, cap, nullptr); break;                      // 29 July 2024 (English)
    case 8:  GetDateFormatEx(L"en-US", 0, &st, L"ddd, d MMMM yyyy", buf, cap, nullptr); break;                 // Mon, 29 July 2024 (English)
    case 9:  GetDateFormatEx(L"en-US", DATE_LONGDATE, &st, nullptr, buf, cap, nullptr); break;                 // Monday, 29 July 2024 (English)
    case 10: case 11: case 12: {
        const wchar_t* sp = (sep == 1) ? L"-" : (sep == 2) ? L"/" : L".";
        wchar_t pic[40];
        if      (fmt == 10) swprintf(pic, 40, L"dd'%s'MM'%s'yyyy", sp, sp);   // dd mm yyyy
        else if (fmt == 11) swprintf(pic, 40, L"MM'%s'dd'%s'yyyy", sp, sp);   // mm dd yyyy
        else                swprintf(pic, 40, L"yyyy'%s'MM'%s'dd", sp, sp);   // yyyy mm dd
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, pic, buf, cap, nullptr);
        break;
    }
    default: GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, buf, cap, nullptr); break;
    }
}
// Example rendering of a date-format option, using a fixed sample date (29 July 2024, a Monday). Used as
// the value + picker label so the user sees exactly what they will get. A localised / English tag is
// appended to the name-bearing formats (which look identical on an English system); numeric formats none.
static std::wstring DateFormatExample(int fmt)
{
    SYSTEMTIME st = {}; st.wYear = 2024; st.wMonth = 7; st.wDay = 29; st.wDayOfWeek = 1;   // Mon 29 Jul 2024
    wchar_t buf[128] = {};
    FormatDateStr(st, fmt, gDateSep, buf, 128);
    std::wstring s = buf;
    if      (fmt >= 5 && fmt <= 9) s += L"  (English)";
    else if (fmt >= 0 && fmt <= 4) s += L"  (localised)";
    return s;
}
static void DrawClock(const MenuLayout& L, ClockPos pos, float alpha)
{
    if (!gD2DCtx || !gBrush || !gShadow || !gClockTimeFmt || !gClockDateFmt || alpha <= 0.01f) return;

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t timeBuf[64] = {}, dateBuf[128] = {};
    // Hour format: follow Windows, or force 12-hour / 24-hour via an explicit picture string.
    if      (gHourFormat == 3) GetTimeFormatEx(L"en-US", 0, &st, L"h:mm tt", timeBuf, 64);              // English 12-hour (AM/PM)
    else if (gHourFormat == 2) GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"HH:mm",    timeBuf, 64);
    else if (gHourFormat == 1) GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"h:mm tt", timeBuf, 64);
    else                       GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, timeBuf, 64);
    // Blinking colon: on odd seconds the ':' is drawn TRANSPARENT (not removed), so its advance width is
    // preserved and the hour/minute digits never shift. 1s-on / 1s-off; menu + screensaver clock alike.
    const bool colonOff = gBlinkColon && (st.wSecond & 1);
    // Date line via the shared formatter (localised / English / numeric presets + separator).
    if (gDateEnabled)
        FormatDateStr(st, gDateFormat, gDateSep, dateBuf, 128);
    UINT32 tlen = (UINT32)lstrlenW(timeBuf);
    UINT32 dlen = (UINT32)lstrlenW(dateBuf);
    if (tlen == 0 && dlen == 0) return;

    const float margin = 40.0f * L.scale;
    const float boxW   = 520.0f * L.scale * gClockScale;
    const float timeH  = L.fontSize * 1.8f * 1.35f * gClockScale;   // time line height
    const float dateH  = (gDateEnabled && dlen) ? L.fontSize * 0.75f * 1.6f * gClockScale : 0.0f; // date line height
    const float blockH = timeH + dateH;
    float left, top;
    DWRITE_TEXT_ALIGNMENT align;
    switch (pos)
    {
    case CLOCK_TL: left = margin;                  top = margin;                     align = DWRITE_TEXT_ALIGNMENT_LEADING;  break;
    case CLOCK_BR: left = (float)gW - margin - boxW; top = (float)gH - margin - blockH; align = DWRITE_TEXT_ALIGNMENT_TRAILING; break;
    case CLOCK_BL: left = margin;                  top = (float)gH - margin - blockH; align = DWRITE_TEXT_ALIGNMENT_LEADING;  break;
    case CLOCK_TR:
    default:       left = (float)gW - margin - boxW; top = margin;                     align = DWRITE_TEXT_ALIGNMENT_TRAILING; break;
    }
    gClockTimeFmt->SetTextAlignment(align);
    gClockDateFmt->SetTextAlignment(align);

    const float so = L.shadowOffset;
    auto line = [&](IDWriteTextFormat* fmt, const wchar_t* txt, UINT32 len, float y, float h)
    {
        D2D1_RECT_F tr = D2D1::RectF(left, y, left + boxW, y + h);
        gShadow->SetOpacity(0.6f * alpha);
        D2D1_RECT_F sr = D2D1::RectF(tr.left + so, tr.top + so, tr.right + so, tr.bottom + so);
        gD2DCtx->DrawText(txt, len, fmt, sr, gShadow);
        gBrush->SetOpacity(alpha);
        gD2DCtx->DrawText(txt, len, fmt, tr, gBrush);
    };
    // Time line via a text layout so the colon can be hidden (a transparent drawing effect on that range)
    // on the blink-off phase WITHOUT changing its advance width - the digits stay put. Plain draw fallback.
    {
        IDWriteTextLayout* lay = nullptr;
        if (SUCCEEDED(gDWrite->CreateTextLayout(timeBuf, tlen, gClockTimeFmt, boxW, timeH, &lay)) && lay)
        {
            lay->SetTextAlignment(align);
            if (colonOff)
                for (UINT32 k = 0; k < tlen; ++k)
                    if (timeBuf[k] == L':') { DWRITE_TEXT_RANGE rg = { k, 1 }; lay->SetDrawingEffect(gClearBrush, rg); }
            gShadow->SetOpacity(0.6f * alpha);
            gD2DCtx->DrawTextLayout(D2D1::Point2F(left + so, top + so), lay, gShadow);
            gBrush->SetOpacity(alpha);
            gD2DCtx->DrawTextLayout(D2D1::Point2F(left, top), lay, gBrush);
            lay->Release();
        }
        else line(gClockTimeFmt, timeBuf, tlen, top, timeH);   // fallback: plain draw (colon still shown)
    }
    if (dateH > 0.0f) line(gClockDateFmt, dateBuf, dlen, top + timeH, dateH);
    gBrush->SetOpacity(1.0f);
    gShadow->SetOpacity(0.6f);
}
