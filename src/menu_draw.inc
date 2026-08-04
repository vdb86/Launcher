// --- draw the menu (ported XMB arrangement) on top of the gradient/bg/ribbon composite ---
// XMB positions (xmb.c): item icons form a column one icon_spacing_horizontal along the category
// axis (under/right of the active category icon); labels a further margins_label_left along it;
// item-axis positions from xmb_item_y (active item splayed). The category row shows ICONS only; the
// selected category name is drawn as a title, top-left. Positions are computed per orientation
// (see the origin block below): horizontal = classic XMB; vertical = left category column + a
// horizontal item strip on the active row. The Settings tab feeds through this SAME loop via the
// active-list helpers (ActiveCount / ActiveSel / ActiveEntry), so settings look identical to items.

// Measure the pixel width of a string in a text format (used to size the selector outline to the label).
static float MeasureTextW(IDWriteTextFormat* fmt, const std::wstring& s)
{
    if (!gDWrite || !fmt || s.empty()) return 0.0f;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(gDWrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, 8192.0f, 8192.0f, &layout)) || !layout)
        return 0.0f;
    DWRITE_TEXT_METRICS m = {};
    layout->GetMetrics(&m);
    layout->Release();
    return m.width;
}
// Measure a string word-wrapped to maxW, capped to maxLines. Returns the used width + height (and line
// count). Titles use this to flow long text to a 2nd line and to size the edit selector to what is drawn.
static void MeasureWrapped(IDWriteTextFormat* fmt, const std::wstring& s, float maxW, int maxLines,
                           float* outW, float* outH, int* outLines)
{
    if (outW) *outW = 0.0f; if (outH) *outH = 0.0f; if (outLines) *outLines = 0;
    if (!gDWrite || !fmt || s.empty() || maxW <= 1.0f) return;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(gDWrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, maxW, 8192.0f, &layout)) || !layout) return;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    DWRITE_TEXT_METRICS m = {};
    layout->GetMetrics(&m);
    layout->Release();
    int lines = (int)m.lineCount; if (maxLines > 0 && lines > maxLines) lines = maxLines;
    if (outW) *outW = m.width;
    // height per line = total / lineCount; cap to maxLines so a 3+ line title still yields a 2-line box.
    float lineH = (m.lineCount > 0) ? (m.height / (float)m.lineCount) : m.height;
    if (outH) *outH = lineH * (float)lines;
    if (outLines) *outLines = lines;
}

static void DrawMenu(const MenuLayout& L)
{
    if (!gD2DCtx || !gD2DTarget || gMenu.empty()) return;
    gD2DCtx->BeginDraw();

    gShadowOffset = L.shadowOffset;                  // used by DrawIcon this frame
    const float so = L.shadowOffset;
    const float ga = gMenuAlpha;                     // global menu fade multiplier

    // Custom background IMAGE (Background = Image): drawn full-screen behind everything, cover-fit
    // (scaled to fill, centre-cropped) over the gradient. The menu + dim scrim draw on top.
    if (BgIsImage() && !gBgImagePath.empty())
    {
        IconTex bg = LoadIcon(gBgImagePath.c_str());
        if (bg.bmp)
        {
            D2D1_SIZE_F bs = bg.bmp->GetSize();
            float bw = bs.width, bh = bs.height, sc = 1.0f;
            if (bw > 0 && bh > 0) sc = ((float)gW / bw > (float)gH / bh) ? ((float)gW / bw) : ((float)gH / bh);
            float dw = bw * sc, dh = bh * sc, cx = gW * 0.5f, cy = gH * 0.5f;
            D2D1_RECT_F rc = D2D1::RectF(cx - dw * 0.5f, cy - dh * 0.5f, cx + dw * 0.5f, cy + dh * 0.5f);
            gD2DCtx->DrawBitmap(bg.bmp, &rc, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        }
    }

    // Screensaver dim: a black scrim over the ribbon that fades IN as the menu fades out, up to the
    // user-chosen percentage. 0% = ribbon stays full brightness (just menu-off); 100% = fades to black.
    if (gScreensaver && gDimEnabled && gSaverDimPercent > 0)
    {
        float dim = (1.0f - gMenuAlpha) * ((float)gSaverDimPercent / 100.0f);
        if (dim > 0.001f)
        {
            gShadow->SetOpacity(dim);
            gD2DCtx->FillRectangle(D2D1::RectF(0, 0, (float)gW, (float)gH), gShadow);
            gShadow->SetOpacity(0.6f);
        }
    }

    // Part C: whole-menu position offset. Every menu element (category row/column, item list, titles,
    // edit outline) is drawn through this translate so they move together; the clock + banners below
    // reset the transform to identity (they are a fixed HUD). Full-screen bg image + dim scrim already
    // drew above, so they stay put.
    gD2DCtx->SetTransform(D2D1::Matrix3x2F::Translation(gMenuPosX, gMenuPosY));

    // Item label alignment: horizontal = right of icon (vert-centred); vertical = centred under icon.
    if (gItemFmt)
    {
        gItemFmt->SetTextAlignment(gVertical ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
        gItemFmt->SetParagraphAlignment(gVertical ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (gItemTitleFmt)   // real item labels use their own (scaled) format; mirror gItemFmt's alignment
    {
        gItemTitleFmt->SetTextAlignment(gVertical ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
        gItemTitleFmt->SetParagraphAlignment(gVertical ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // --- item list (active category, OR the Settings sections / section rows) ---
    const int   listCount = gEditOnAddCat ? 0 : ActiveCount();   // add-category slot shows no item list
    const int   sel      = ActiveSel();              // selected index within the active list
    const float listFade = gListFade.cur;            // whole-list fade on category switch
    const float catTween = gCatTween.cur;

    // Per-orientation origins. HORIZONTAL (classic XMB): categories run along the top X row; the
    // active category's items form a vertical column under it (splayed via xmb_item_y). VERTICAL
    // (reworked): categories run down a left column; the active category's items form an evenly
    // spaced horizontal strip to the RIGHT of that column, on the active category's row.
    const int   fItemVis = gFixedLayout ? VisibleItemSlots(L) : 0;
    const int   fCatVis  = gFixedLayout ? VisibleCatSlots(L)  : 0;
    // Fixed placement: the whole category block (incl. the synthetic Settings tab - a real gMenu entry)
    // is ANCHORED along its run by the per-orientation align setting. A HORIZONTAL menu uses alignH
    // (Left / Center / Right along X); a VERTICAL menu uses alignV (Top / Center / Bottom along Y). Only
    // the one that matches the orientation is shown (see SettingIsHidden) - it is the menu's anchor point,
    // which the future per-menu MOVE feature will build on. The perpendicular axis keeps its classic
    // position. If the block does not fit the axis, pin to the classic start anchor and let it scroll.
    const float vLeft = 48.0f * L.scale;             // vertical category column near the left edge
    float fixCatBase = 0.0f;                         // centre (px) of category slot 0 along the run axis
    if (gFixedLayout)
    {
        const float pad = 40.0f * L.scale;
        float span   = gVertical ? (float)gH : (float)gW;
        float anchor = gVertical ? (L.marginTop + L.iconSize) : (L.marginLeft + L.catPitch);
        int   cn     = (int)gMenu.size();
        float block  = L.catPitch * (float)cn;       // full category block length along its run
        int   a      = gVertical ? CurTune().alignV : CurTune().alignH;   // this orientation's anchor
        if (cn <= fCatVis)                           // whole block fits on the axis: anchor start/centre/end
            fixCatBase = (a == 0) ? (pad + L.catPitch * 0.5f)
                       : (a == 2) ? (span - block - pad + L.catPitch * 0.5f)
                                  : ((span - block) * 0.5f + L.catPitch * 0.5f);   // centre (default)
        else
            fixCatBase = anchor;                     // overflow: classic start anchor, window scrolls
    }
    const float catRowY = L.marginTop + L.iconSize * 0.5f;   // horizontal category-row icon-centre Y (classic)

    float itemColX = 0, itemBaseY = 0;               // horizontal item column x / list-top y
    float catColX = 0, catActiveY = 0, itemsBaseX = 0, rowY = 0, yCat0 = 0; // vertical origins
    if (!gVertical)
    {
        // Scrolling: the active category is pinned one slot in and the item column sits under it.
        // Fixed: the category block is aligned (fixCatBase); the item column sits under the HIGHLIGHTED
        // category (tweened so it glides on a category change), below the category row (catRowY).
        itemColX  = gFixedLayout
            ? (fixCatBase + L.catPitch * (gFixCatSelT.cur - gFixCatScrollT.cur))
            : (L.marginLeft + L.catPitch);
        itemBaseY = gFixedLayout ? (catRowY + L.iconSize * 1.4f) : (L.marginTop + L.iconSize * 0.5f);
        itemBaseY += L.iconSize * (CurTune().spCatItem - 1.0f);   // user gap between the category row and the item list
    }
    else
    {
        catColX    = vLeft + L.iconSize * 0.5f;      // classic left column (cross axis stays put)
        catActiveY = (float)gH * 0.42f;              // active category / item row baseline
        // item strip starts 1.5 icons to the right of the category-icon centre (+ user cat/item gap).
        itemsBaseX = catColX + L.iconSize * 1.5f + L.iconSize * (CurTune().spCatItem - 1.0f);
        yCat0      = gFixedLayout ? fixCatBase : (L.marginTop + L.iconSize);  // fixed: centred column origin
        // Scrolling: the active category / item row is centred at 0.42h. Fixed: the item strip sits
        // on the highlighted category's (tweened) fixed row so the two read as belonging together.
        rowY = gFixedLayout ? (fixCatBase + L.catPitch * (gFixCatSelT.cur - gFixCatScrollT.cur)) : catActiveY;
    }
    // Part C2: submenu-flow DIRECTION (per orientation x placement, applies to every category in the mode).
    // A reversed menu is the WHOLE menu mirrored about the screen centre on the flow axis: the category bar
    // moves to the OPPOSITE edge (top row -> bottom, left column -> right) and the submenu flows inward from
    // it, so the reversed layout is a full mirror of the (working) forward layout and stays entirely on
    // screen. reflectPt()/reflectRect() mirror a menu point/rect about the screen centre; forward
    // (subRev = false) = identity so the default look is byte-identical. (The item-axis input is inverted in
    // ApplyMenuAction/EditNav so pressing the visual direction still walks 1->2->3->4.)
    const bool subRev = CurTune().subReverse;
    auto reflectPt = [&](D2D1_POINT_2F pt) {
        if (subRev) { if (gVertical) pt.x = (float)gW - pt.x; else pt.y = (float)gH - pt.y; }
        return pt;
    };
    auto reflectRect = [&](D2D1_RECT_F r) {
        if (subRev) {
            if (gVertical) { float l = (float)gW - r.right, rr = (float)gW - r.left; r.left = l; r.right = rr; }
            else           { float t = (float)gH - r.bottom, b = (float)gH - r.top;  r.top = t;  r.bottom = b; }
        }
        return r;
    };
    // Rounded-rect stroke used as the EDIT-MODE selector. In normal browsing there is no cursor box -
    // the enlarged selected element is the cue (user request); the outline is shown only in edit mode,
    // around whatever is focused (an item, or the category tab), and marks the target in move mode.
    auto outline = [&](float left, float top, float right, float bottom, float a)
    {
        if (!gBrush || !gShadow) return;
        const float r = 10.0f * L.scale, sw = 2.5f * L.scale;
        D2D1_ROUNDED_RECT rs = { D2D1::RectF(left + so, top + so, right + so, bottom + so), r, r };
        D2D1_ROUNDED_RECT rr = { D2D1::RectF(left, top, right, bottom), r, r };
        gShadow->SetOpacity(0.45f * a);
        gD2DCtx->DrawRoundedRectangle(rs, gShadow, sw);
        gBrush->SetOpacity(0.95f * a);
        gD2DCtx->DrawRoundedRectangle(rr, gBrush, sw);
        gBrush->SetOpacity(1.0f);
    };
    if (gEditMode)
    {
        // Focused ITEM: box around icon + label (both placements/orientations, using the same slot math
        // the icons draw with, so the box lines up exactly).
        // (sel may be the end-of-list "+" add slot; ActiveEntry gives its label.)
        if (!gEditFocusCat && !gEditOnAddCat && sel >= 0 && sel < listCount && sel < (int)gItemYT.size())
        {
            std::wstring et, ev, ei; ActiveEntry(sel, et, ev, ei);
            D2D1_POINT_2F p;
            if (gFixedLayout)
            {
                float hs = gFixItemSelT.cur - gFixItemScrollT.cur;
                p = gVertical ? D2D1::Point2F(itemsBaseX + L.itemPitchV * hs, rowY)
                              : D2D1::Point2F(itemColX, itemBaseY + FixedColPitchV(L) * hs + L.iconSize * 0.5f);
            }
            else
            {
                float ny = gItemYT[sel].cur;
                p = gVertical ? D2D1::Point2F(itemsBaseX + ny, rowY)
                              : D2D1::Point2F(itemColX, itemBaseY + ny + L.iconSize * 0.5f);
            }
            p = reflectPt(p);                                   // reversed submenu: mirror to the opposite side of the bar
            gEditAnchorX = p.x + gMenuPosX; gEditAnchorY = p.y + gMenuPosY; gEditAnchorHalf = L.iconSize * 0.6f;  // for the edit popup (+ menu offset)
            if (gVertical)
            {
                bool showLbl = CurTune().itemTitleShow && !et.empty();
                float hwL = L.itemPitchV * 0.45f, lh = L.fontSize * 1.5f;   // match the label draw width (pitch-based)
                float mW = 0.0f, mH = lh; int lines = 1;
                if (showLbl) { MeasureWrapped(gItemTitleFmt ? gItemTitleFmt : gItemFmt, et, hwL * 2.0f, 2, &mW, &mH, &lines);
                               if (lines < 2) mH = lh; }
                float boxHalf = L.iconSize * 0.62f;
                if (showLbl) { float labHalf = mW * 0.5f + L.iconSize * 0.2f; if (labHalf > boxHalf) boxHalf = labHalf; }
                float ly  = p.y + L.iconSize * 0.6f * CurTune().spIconTitle;
                float bot = showLbl ? (ly + mH + L.fontSize * 0.22f) : (p.y + L.iconSize * 0.6f);  // + descender pad
                outline(p.x - boxHalf, p.y - L.iconSize * 0.6f, p.x + boxHalf, bot, ga);
            }
            else
            {
                float lw   = MeasureTextW(gItemFmt, et);
                float left = p.x - L.iconSize * 0.6f;
                float rght = itemColX + L.labelLeft + lw + L.iconSize * 0.35f;
                float hh   = L.iconSize * 0.62f;
                outline(left, p.y - hh, rght, p.y + hh, ga);
            }
        }
        // Focused CATEGORY tab (or the "+" add-category slot at index realCount): box around its icon.
        if (gEditFocusCat)
        {
            int realCount = (int)gMenu.size();
            float catIdx = gEditOnAddCat ? (float)realCount : (float)gCatSel;
            D2D1_POINT_2F cp;
            if (gFixedLayout)
            {
                float chs = gEditOnAddCat ? ((float)realCount - gFixCatScrollT.cur)
                                          : (gFixCatSelT.cur - gFixCatScrollT.cur);
                cp = gVertical ? D2D1::Point2F(catColX, yCat0 + L.catPitch * chs)
                               : D2D1::Point2F(fixCatBase + L.catPitch * chs, catRowY);
            }
            else
            {
                cp = gVertical ? D2D1::Point2F(catColX, catActiveY + L.catPitch * (catIdx - catTween))
                               : D2D1::Point2F(L.marginLeft + L.catPitch * (catIdx + 1.0f - catTween), catRowY);
            }
            D2D1_POINT_2F cpFwd = cp;                          // pre-reflection centre (for the title extent)
            cp = reflectPt(cp);                                // reversed menu: category bar mirrors to the opposite edge
            float cr = L.iconSize * 0.72f;
            gEditAnchorX = cp.x + gMenuPosX; gEditAnchorY = cp.y + gMenuPosY; gEditAnchorHalf = cr;   // for the edit popup (+ menu offset)
            // Enclose the category TITLE drawn below the icon (always shown while editing), not just the
            // icon: widen to the title width and extend down to its baseline. reflectRect mirrors the box
            // for a reversed menu (title above the icon). Clamp to the screen so it never runs off-edge.
            std::wstring ctitle = gEditOnAddCat ? std::wstring(L"Add")
                                : (gCatSel >= 0 && gCatSel < (int)gMenu.size()) ? gMenu[gCatSel].title : std::wstring();
            IDWriteTextFormat* ctf = gCatTitleFmt ? gCatTitleFmt : gItemFmt;
            float titleTh = L.fontSize * 1.5f * CurTune().catTitleScale;    // one-line rect height (classic)
            float tW = 0.0f, tH = titleTh; int tLines = 1;
            if (!ctitle.empty()) { MeasureWrapped(ctf, ctitle, L.spacingH * 4.0f, 2, &tW, &tH, &tLines);
                                   if (tLines < 2) tH = titleTh; }           // 1 line keeps the classic height
            float tHalf  = tW * 0.5f + L.iconSize * 0.25f;
            float halfW  = (tHalf > cr) ? tHalf : cr;
            float catGap = 6.0f * L.scale + L.iconSize * (CurTune().catIconTitle - 1.0f) * 0.6f;
            float botFwd = cpFwd.y + L.iconSize * 0.5f + catGap + tH + L.fontSize * 0.22f;  // title bottom + descender pad
            D2D1_RECT_F cbox = reflectRect(D2D1::RectF(cpFwd.x - halfW, cpFwd.y - cr, cpFwd.x + halfW, botFwd));
            float m = 12.0f * L.scale;
            if (cbox.left  < m)             cbox.left  = m;
            if (cbox.right > (float)gW - m) cbox.right = (float)gW - m;
            outline(cbox.left, cbox.top, cbox.right, cbox.bottom, ga);
        }
    }

    // Horizontal XMB edge fade (ported xmb.c ~6011): fades rows toward the top/bottom screen edges.
    const float vff        = 100.0f;                 // menu_xmb_vertical_fade_factor (default)
    const float iconSpace  = L.spacingV;
    const float iconRatio  = 4.0f / (float)gH;
    const float scrMargin  = L.marginTop + (iconSpace / iconRatio / 400.0f);
    const float fadeFactor = (vff / 100.0f) / iconRatio;

    for (int i = 0; i < listCount && i < (int)gItemYT.size(); ++i)
    {
        std::wstring eTitle, eValue, eIcon;          // active-list entry (item OR settings row/section)
        ActiveEntry(i, eTitle, eValue, eIcon);

        float ny = gItemYT[i].cur;                   // along-list offset (see RetargetItems)
        float par = (i < (int)gItemXT.size()) ? gItemXT[i].cur : 0.0f; // list-switch slide magnitude
        float nx  = (gListSlideAxis == 0) ? par : 0.0f;  // Slide: offset ACROSS the bar (X)
        float nyS = (gListSlideAxis == 1) ? par : 0.0f;  // Axis slide: offset ALONG the list (Y)

        D2D1_POINT_2F p;
        if (gFixedLayout)
        {
            float slotV = (float)i - gFixItemScrollT.cur;   // continuous slot; list glides on scroll
            if (slotV < -1.0f || slotV > (float)fItemVis) continue;   // outside the visible window
            p = gVertical
                ? D2D1::Point2F(itemsBaseX + L.itemPitchV * slotV + nx, rowY + nyS)
                : D2D1::Point2F(itemColX + nx, itemBaseY + FixedColPitchV(L) * slotV + L.iconSize * 0.5f + nyS);
        }
        else
        {
            p = gVertical
                ? D2D1::Point2F(itemsBaseX + ny + nx, rowY + nyS)
                : D2D1::Point2F(itemColX + nx, itemBaseY + ny + L.iconSize * 0.5f + nyS);
        }
        p = reflectPt(p);                                    // reversed submenu: mirror to the opposite side of the bar
        if (p.x < -L.iconSize * 2 || p.x >(float)gW + L.iconSize * 2 ||
            p.y < -L.iconSize * 2 || p.y >(float)gH + L.iconSize * 2) continue;

        float alpha = 0.75f + 0.25f * gItemAT[i].cur; // passive 0.75 -> active 1.0 (out_circ)
        float zoom  = 0.5f  + 0.5f  * gItemZT[i].cur; // passive 0.5  -> active 1.0 (out_quad)
        // Selection cue is enlargement (the selector box is edit-mode only). In fixed placement with
        // "Enlarge selected" Off, every icon instead stays full size and brightness alone marks the
        // selection (item titles are always shown) - dim the unselected so the bright one stands out.
        if (gFixedLayout && !gEnlargeSelected && !gEditMode) {
            zoom  = 1.0f;
            alpha = 0.5f + 0.5f * gItemAT[i].cur;     // passive 0.5 -> active 1.0 (brightness cue)
        }
        if (gFixedLayout)
        {
            // Fixed placement: no XMB edge fade (the list is windowed; selector drawn above).
        }
        else if (subRev)
        {
            // Reversed flow (up/left): the forward top/bottom (and left/right) fade formulas below assume
            // the default direction, so skip them here - the passive-alpha dimming is enough.
        }
        else if (!gVertical)                         // XMB vertical fade toward top/bottom edges
        {
            float na = alpha;
            if      (i < sel) na = (ny + scrMargin) / fadeFactor;
            else if (i > sel) na = ((float)gH - ny - scrMargin + iconSpace) / fadeFactor;
            if (na < 0.01f) na = 0.01f;
            if (na < alpha) alpha = na;
        }
        else                                         // fade the strip toward the left/right edges
        {
            float span  = L.itemPitchV * 1.5f;
            float fadeR = (float)gW - 40.0f * L.scale;
            if (p.x < itemsBaseX) { float f = 1.0f - (itemsBaseX - p.x) / span; alpha *= (f < 0.1f ? 0.1f : f); }
            if (p.x > fadeR)      { float f = 1.0f - (p.x - fadeR) / span;      alpha *= (f < 0.1f ? 0.1f : f); }
        }
        if (gFixedLayout) { alpha *= ga * listFade; } // list-switch offset (nx/nyS) now applies in fixed too
        else                alpha *= ga * listFade;             // global menu fade * list-switch fade
        DrawEntryIcon(eIcon, p.x, p.y, L.iconSize * zoom * CurTune().itemIconScale, alpha);  // handles "menu_add"/"apps" sentinels + file paths

        // Label size is CONSTANT in both placements now (classic XMB): only the icon zooms on
        // selection. This keeps fixed-placement unfocused items the same visual size as scrolling
        // (previously the label scaled with the icon in fixed mode, so passive labels looked tiny).
        const float txtZoom = 1.0f;
        UINT32 len = (UINT32)eTitle.size();
        gBrush->SetOpacity(alpha);
        gShadow->SetOpacity(0.6f * alpha);
        auto drawStr = [&](const std::wstring& s, IDWriteTextFormat* fmt, D2D1_RECT_F rect, ID2D1Brush* br, float ax, float ay) {
            if (txtZoom != 1.0f) gD2DCtx->SetTransform(D2D1::Matrix3x2F::Scale(txtZoom, txtZoom, D2D1::Point2F(ax, ay)));
            gD2DCtx->DrawText(s.c_str(), (UINT32)s.size(), fmt, rect, br);
            if (txtZoom != 1.0f) gD2DCtx->SetTransform(D2D1::Matrix3x2F::Identity());
        };
        // Item labels use gItemTitleFmt (scaled per current layout) and honour the item-title on-off toggle.
        auto drawLabel = [&](D2D1_RECT_F rect, ID2D1Brush* br, float ax, float ay) {
            if (!CurTune().itemTitleShow) return;            // item labels hidden (icon-only look)
            drawStr(eTitle, gItemTitleFmt, rect, br, ax, ay);
        };
        if (!gVertical)                              // label right of icon, vertically centred
        {
            float lx = itemColX + L.labelLeft + nx;
            float th = L.spacingV;
            float ly = p.y - L.fontSize * 0.18f;         // nudge the item title up a touch (was centred on the icon)
            drawLabel(D2D1::RectF(lx + so, ly - th * 0.5f + so, (float)gW, ly + th * 0.5f + so), gShadow, lx, ly);
            drawLabel(D2D1::RectF(lx, ly - th * 0.5f, (float)gW, ly + th * 0.5f), gBrush, lx, ly);
        }
        else                                         // label centred under the icon
        {
            float ly = p.y + L.iconSize * 0.6f * CurTune().spIconTitle;  // icon-to-title gap (scaled)
            // Label box half-width = ~0.45 of the item pitch, so labels never overlap their neighbours and a
            // label wider than its slot wraps to a 2nd line (rect is 2 lines tall; top-aligned = 1 line unchanged).
            float hw = L.itemPitchV * 0.45f, lh2 = L.fontSize * 1.5f * 2.0f;
            drawLabel(D2D1::RectF(p.x - hw + so, ly + so, p.x + hw + so, ly + lh2 + so), gShadow, p.x, ly);
            drawLabel(D2D1::RectF(p.x - hw, ly, p.x + hw, ly + lh2), gBrush, p.x, ly);
        }
    }
    gBrush->SetOpacity(1.0f);
    gShadow->SetOpacity(0.6f);

    // --- category icons (icons only) ---
    for (int c = 0; c < (int)gMenu.size(); ++c)
    {
        D2D1_POINT_2F p;
        if (gFixedLayout)
        {
            float cslotV = (float)c - gFixCatScrollT.cur;   // continuous slot; column glides on scroll
            if (cslotV < -1.0f || cslotV > (float)fCatVis) continue;
            p = gVertical
                ? D2D1::Point2F(catColX, yCat0 + L.catPitch * cslotV)
                : D2D1::Point2F(fixCatBase + L.catPitch * cslotV, catRowY);
        }
        else
        {
            p = gVertical
                ? D2D1::Point2F(catColX, catActiveY + L.catPitch * ((float)c - catTween))
                : D2D1::Point2F(L.marginLeft + L.catPitch * ((float)c + 1.0f - catTween), catRowY);
        }
        p = reflectPt(p);                                    // reversed menu: category bar mirrors to the opposite edge
        if (p.x < -L.iconSize * 2 || p.x >(float)gW + L.iconSize * 2 ||
            p.y < -L.iconSize * 2 || p.y >(float)gH + L.iconSize * 2) continue;
        float dist = fabsf((float)c - catTween);
        float t = 1.0f - dist; if (t < 0) t = 0; if (t > 1) t = 1;
        float alpha = (0.75f + 0.25f * t) * ga;    // categories_passive_alpha 0.75 -> active 1.0
        float zoom  = 0.5f  + 0.5f  * t;           // categories_passive_zoom 0.5 -> active 1.0
        // Fixed placement, "Enlarge selected" Off: uniform-size categories, brightness-only cue.
        if (gFixedLayout && !gEnlargeSelected && !gEditMode) {
            zoom  = 1.0f;
            alpha = (0.5f + 0.5f * t) * ga;
        }
        // (fixed-placement category selector is drawn above the loops so it glides)
        DrawEntryIcon(gMenu[c].icon, p.x, p.y, L.iconSize * zoom * CurTune().catIconScale, alpha);  // sentinels ("apps"/"menu_add") + file paths
    }

    // Edit mode: the "+" add-category slot, drawn just past the last category.
    if (gEditMode)
    {
        int realCount = (int)gMenu.size();
        bool draw = true;
        D2D1_POINT_2F p;
        if (gFixedLayout)
        {
            float cslotV = (float)realCount - gFixCatScrollT.cur;
            if (cslotV < -1.0f || cslotV > (float)fCatVis) draw = false;
            p = gVertical
                ? D2D1::Point2F(catColX, yCat0 + L.catPitch * cslotV)
                : D2D1::Point2F(fixCatBase + L.catPitch * cslotV, catRowY);
        }
        else
        {
            p = gVertical
                ? D2D1::Point2F(catColX, catActiveY + L.catPitch * ((float)realCount - catTween))
                : D2D1::Point2F(L.marginLeft + L.catPitch * ((float)realCount + 1.0f - catTween), catRowY);
        }
        p = reflectPt(p);                                    // reversed menu: category bar mirrors to the opposite edge
        if (draw)
        {
            float dist = fabsf((float)realCount - catTween);
            float t = 1.0f - dist; if (t < 0) t = 0; if (t > 1) t = 1;
            float a = (0.75f + 0.25f * t) * ga;
            float z = 0.5f + 0.5f * t;
            DrawPlusGlyph(p.x, p.y, L.iconSize * z * CurTune().catIconScale, a);   // "+" add-category slot (vector plus)
        }
    }

    // --- all category titles (option): label EVERY category under its icon, not only the active one ---
    // Browse only (edit mode keeps the active-only label + its banner / "+" handling below). Active is
    // brightest, the rest dimmer. Each title is placed with the same math as the active one, per category.
    if (CurTune().catTitleShow && CurTune().catTitleAll && !gEditMode)
    {
        IDWriteTextFormat* tf = gCatTitleFmt ? gCatTitleFmt : gItemFmt;
        const float th    = L.fontSize * 1.5f * CurTune().catTitleScale;
        const float halfW = gVertical ? L.spacingH : L.catPitch * 0.48f;   // narrow so neighbours don't collide
        for (int c = 0; c < (int)gMenu.size(); ++c)
        {
            D2D1_POINT_2F pf;                                     // forward icon centre (before reflect)
            if (gFixedLayout)
            {
                float cslotV = (float)c - gFixCatScrollT.cur;
                if (cslotV < -1.0f || cslotV > (float)fCatVis) continue;
                pf = gVertical ? D2D1::Point2F(catColX, yCat0 + L.catPitch * cslotV)
                               : D2D1::Point2F(fixCatBase + L.catPitch * cslotV, catRowY);
            }
            else
            {
                pf = gVertical ? D2D1::Point2F(catColX, catActiveY + L.catPitch * ((float)c - catTween))
                               : D2D1::Point2F(L.marginLeft + L.catPitch * ((float)c + 1.0f - catTween), catRowY);
            }
            D2D1_POINT_2F pr = reflectPt(pf);
            if (pr.x < -L.iconSize * 2 || pr.x >(float)gW + L.iconSize * 2 ||
                pr.y < -L.iconSize * 2 || pr.y >(float)gH + L.iconSize * 2) continue;
            float t = 1.0f - fabsf((float)c - catTween); if (t < 0) t = 0; if (t > 1) t = 1;
            float a = (0.55f + 0.45f * t) * ga;                  // active bright, others dimmer
            std::wstring title = gMenu[c].title;
            float mW = 0.0f, mH = th; int mLines = 1;
            MeasureWrapped(tf, title, halfW * 2.0f, 2, &mW, &mH, &mLines);
            const float rectH = (mLines >= 2) ? mH : th;
            const float tyTop = pf.y + L.iconSize * 0.5f + 6.0f * L.scale
                              + L.iconSize * (CurTune().catIconTitle - 1.0f) * 0.6f;
            gShadow->SetOpacity(0.6f * a);
            D2D1_RECT_F ts = reflectRect(D2D1::RectF(pf.x - halfW + so, tyTop + so, pf.x + halfW + so, tyTop + rectH + so));
            gD2DCtx->DrawText(title.c_str(), (UINT32)title.size(), tf, ts, gShadow);
            gBrush->SetOpacity(a);
            D2D1_RECT_F tr = reflectRect(D2D1::RectF(pf.x - halfW, tyTop, pf.x + halfW, tyTop + rectH));
            gD2DCtx->DrawText(title.c_str(), (UINT32)title.size(), tf, tr, gBrush);
        }
        gBrush->SetOpacity(1.0f);
    }
    // --- active category title: centred directly BELOW the active category icon (was the top-left corner) ---
    // Skipped entirely when the user turns the category title off. (Always shown while editing / on the
    // "+" add slot so the target stays labelled.) Skipped when "all titles" already drew them (browse).
    else if (CurTune().catTitleShow || gEditMode)
    {
        std::wstring title = gMenu[gCatSel].title;
        if (gEditOnAddCat) title = L"Add";                 // on the "+" add slot
        // Active category icon centre (mirrors the category-icon draw math), so the title sits under it.
        float acx, acy;
        if (!gVertical)
        {
            acy = catRowY;
            acx = gFixedLayout ? (fixCatBase + L.catPitch * (gFixCatSelT.cur - gFixCatScrollT.cur))
                               : (L.marginLeft + L.catPitch * ((float)gCatSel + 1.0f - catTween));
        }
        else { acx = catColX; acy = rowY; }
        // Draw with gCatTitleFmt (own size, scaled by catTitleScale; already centre-aligned).
        IDWriteTextFormat* tf = gCatTitleFmt ? gCatTitleFmt : gItemFmt;
        const float th    = L.fontSize * 1.5f * CurTune().catTitleScale;
        const float halfW = L.spacingH * 2.0f;
        float mW = 0.0f, mH = th; int mLines = 1;                          // wrap a long title to a 2nd line
        MeasureWrapped(tf, title, halfW * 2.0f, 2, &mW, &mH, &mLines);
        const float rectH = (mLines >= 2) ? mH : th;                       // 1 line = classic height (unchanged)
        const float tyTop = acy + L.iconSize * 0.5f + 6.0f * L.scale
                          + L.iconSize * (CurTune().catIconTitle - 1.0f) * 0.6f;   // user category icon-to-title gap
        gShadow->SetOpacity(0.6f * ga);
        // reflectRect mirrors the title to the icon's item-side when the menu is reversed (above the icon
        // for a bottom row / correct side for a right column), matching the mirrored bar + submenu.
        D2D1_RECT_F ts = reflectRect(D2D1::RectF(acx - halfW + so, tyTop + so, acx + halfW + so, tyTop + rectH + so));
        gD2DCtx->DrawText(title.c_str(), (UINT32)title.size(), tf, ts, gShadow);
        gBrush->SetOpacity(ga);
        D2D1_RECT_F tr = reflectRect(D2D1::RectF(acx - halfW, tyTop, acx + halfW, tyTop + rectH));
        gD2DCtx->DrawText(title.c_str(), (UINT32)title.size(), tf, tr, gBrush);
    }
    gBrush->SetOpacity(1.0f);

    gD2DCtx->SetTransform(D2D1::Matrix3x2F::Identity());   // Part C: stop the menu offset - clock + banners are a fixed HUD

    // --- clock + date HUD ---
    // Always-on menu clock (top-right), fading with the menu (ga = gMenuAlpha). During the
    // screensaver an optional second clock fades IN as the menu fades out (alpha = 1 - gMenuAlpha).
    // The screensaver clock hops to a new corner every minute: clockwise, counterclockwise, or a
    // random corner. There is no centre position.
    DrawClock(L, (ClockPos)gClockCorner, ga);
    if (gScreensaver && gSaverClockEnabled)
    {
        SYSTEMTIME st; GetLocalTime(&st);
        if (gClockLastMin < 0) gClockLastMin = st.wMinute;         // first frame: no hop
        else if (st.wMinute != gClockLastMin)
        {
            gClockLastMin = st.wMinute;
            if      (gSaverClockMotion == 0) gSaverCorner = (gSaverCorner + 1) % 4;  // clockwise
            else if (gSaverClockMotion == 1) gSaverCorner = (gSaverCorner + 3) % 4;  // counterclockwise
            else { int nc = gSaverCorner; while (nc == gSaverCorner) nc = rand() % 4; gSaverCorner = nc; } // random
        }
        DrawClock(L, kCwCorners[gSaverCorner], 1.0f - gMenuAlpha);
    }

    // (Settings is now a gear tab in the menu, so no separate on-screen hint is needed.)
    // Edit-mode banner: a clear "EDIT MODE" strip along the top explaining the controls, so the user
    // knows OK now edits the highlighted item (rather than launching it) and how to add / leave.
    if (ga > 0.05f && gEditMode)
    {
        const float sso = L.shadowOffset;
        std::wstring banner = gMoveMode
            ? std::wstring(L"MOVE MODE   D-pad: reposition (across categories too)   OK / B: drop")
            : gEditOnAddCat
                ? std::wstring(L"EDIT MODE   OK: add category or item   left: back   B: exit")
                : (gEditFocusCat
                    ? std::wstring(L"EDIT MODE - CATEGORY   OK: options   down: into items   right: + new category   B: exit")
                    : std::wstring(L"EDIT MODE - ITEM   OK: options (last row + = add item)   up: category   B: exit"));
        float by = 40.0f * L.scale;
        gSubFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        gSubFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_RECT_F br = D2D1::RectF(0, by, (float)gW, by + L.fontSize * 1.6f);
        gShadow->SetOpacity(0.6f * ga);
        gD2DCtx->DrawText(banner.c_str(), (UINT32)banner.size(), gSubFmt,
                          D2D1::RectF(br.left + sso, br.top + sso, br.right + sso, br.bottom + sso), gShadow);
        gBrush->SetOpacity(0.9f * ga);
        gD2DCtx->DrawText(banner.c_str(), (UINT32)banner.size(), gSubFmt, br, gBrush);
        gSubFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        gSubFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        gBrush->SetOpacity(1.0f);
    }

    // Part C: "Menu position" nudge-mode banner (same top strip as edit mode).
    if (ga > 0.05f && gPosMode)
    {
        const float sso = L.shadowOffset;
        std::wstring banner = L"MOVE MENU   D-pad: nudge position   OK: save   B: cancel";
        float by = 40.0f * L.scale;
        gSubFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        gSubFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_RECT_F br = D2D1::RectF(0, by, (float)gW, by + L.fontSize * 1.6f);
        gShadow->SetOpacity(0.6f * ga);
        gD2DCtx->DrawText(banner.c_str(), (UINT32)banner.size(), gSubFmt,
                          D2D1::RectF(br.left + sso, br.top + sso, br.right + sso, br.bottom + sso), gShadow);
        gBrush->SetOpacity(0.9f * ga);
        gD2DCtx->DrawText(banner.c_str(), (UINT32)banner.size(), gSubFmt, br, gBrush);
        gSubFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        gSubFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        gBrush->SetOpacity(1.0f);
    }

    // Two-press exit prompt (RetroArch quit_press_twice): shown after the first quit press, fades out.
    if (gQuitPromptTimer > 0.0f && gItemFmt)
    {
        float a = gQuitPromptTimer / 500.0f; if (a > 1.0f) a = 1.0f;   // fade out over the last 500ms
        const wchar_t* msg = L"Press Esc / B again to exit";
        float bw = (float)gW * 0.5f, bh = L.fontSize * 2.4f;
        float bx = ((float)gW - bw) * 0.5f, by = (float)gH - bh - 40.0f * L.scale;
        D2D1_ROUNDED_RECT qr = { D2D1::RectF(bx, by, bx + bw, by + bh), 12.0f * L.scale, 12.0f * L.scale };
        gShadow->SetOpacity(0.85f * a); gD2DCtx->FillRoundedRectangle(qr, gShadow);
        gItemFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        gItemFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        gBrush->SetOpacity(a);
        gD2DCtx->DrawText(msg, (UINT32)wcslen(msg), gItemFmt, D2D1::RectF(bx, by, bx + bw, by + bh), gBrush);
        gBrush->SetOpacity(1.0f);
        gItemFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    // Startup hint: how to open Settings (bottom strip, fades over its last second). Only on the plain
    // browse view, and not during the screensaver.
    if (gSettingsHintMs > 0.0f && ga > 0.05f && !gScreensaver &&
        gUi == UI_BROWSE && !gEditMode && !gPosMode && gSubFmt)
    {
        float a = gSettingsHintMs / 1000.0f; if (a > 1.0f) a = 1.0f;   // fade out over the final second
        const wchar_t* msg = L"Settings:  hold OK  -  or press Start / Tab";
        float bh = L.fontSize * 2.2f, bw = (float)gW * 0.6f;
        float bx = ((float)gW - bw) * 0.5f, by = (float)gH - bh - 34.0f * L.scale;
        D2D1_ROUNDED_RECT hr = { D2D1::RectF(bx, by, bx + bw, by + bh), 12.0f * L.scale, 12.0f * L.scale };
        gShadow->SetOpacity(0.7f * a); gD2DCtx->FillRoundedRectangle(hr, gShadow);
        gSubFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        gSubFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        gBrush->SetOpacity(0.92f * a);
        gD2DCtx->DrawText(msg, (UINT32)wcslen(msg), gSubFmt, D2D1::RectF(bx, by, bx + bw, by + bh), gBrush);
        gBrush->SetOpacity(1.0f);
        gSubFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        gSubFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    gD2DCtx->EndDraw();
}
