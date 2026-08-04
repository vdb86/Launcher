// --- icons (Monochrome theme, tinted at draw time) ---
static IWICImagingFactory*    gWIC = nullptr;
static ID2D1SolidColorBrush*  gIconBrush = nullptr;              // tint brush (colour set per draw)
static std::wstring           gUserIconDir;                      // ...\assets\icons\ (user's own images; browse default)
struct IconTex { ID2D1Bitmap1* bmp = nullptr; float norm = 1.0f; bool colour = false; };
// norm = content-size normaliser (monochrome glyphs); colour = draw as a full-colour image (a user
// PNG/JPEG chosen via the file browser) rather than a tinted monochrome mask.
static std::unordered_map<std::wstring, IconTex> gIconCache;
static bool  gIconInverted = false;  // false = Monochrome (white icons), true = Monochrome Inverted (black)
static float gShadowOffset = 2.0f;   // px, set per-frame from layout (xmb shadow_offset)

// Find the user's own icon-image folder (assets\icons) by walking up from the exe dir. This is where
// the user drops their own images, and the folder the icon file-browser opens in by default.
static void ResolveUserIconDir()
{
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    size_t p = dir.find_last_of(L"\\/");
    if (p != std::wstring::npos) dir = dir.substr(0, p + 1);
    std::wstring cand = dir;
    for (int up = 0; up < 6; ++up)
    {
        std::wstring test = cand + L"assets\\icons";
        DWORD a = GetFileAttributesW(test.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
        { gUserIconDir = test + L"\\"; return; }
        std::wstring t = cand;
        if (!t.empty() && (t.back() == L'\\' || t.back() == L'/')) t.pop_back();
        size_t q = t.find_last_of(L"\\/");
        if (q == std::wstring::npos) break;
        cand = t.substr(0, q + 1);
    }
    // Not found: gUserIconDir stays empty; the browser falls back to the drive list.
}

static void InitIcons()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     __uuidof(IWICImagingFactory), (void**)&gWIC);
    ResolveUserIconDir();
}

// True if the extension is a supported raster image type.
static bool HasImageExt(const std::wstring& s)
{
    size_t dot = s.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = s.substr(dot);
    return !_wcsicmp(ext.c_str(), L".png")  || !_wcsicmp(ext.c_str(), L".jpg") ||
           !_wcsicmp(ext.c_str(), L".jpeg") || !_wcsicmp(ext.c_str(), L".bmp") ||
           !_wcsicmp(ext.c_str(), L".gif");
}
// An icon string is a FILE PATH: it has a path separator / drive colon, or an image extension. Icons
// are always file paths now (the bundled glyph-name set was removed); this only splits image files
// (drawn in colour) from exe/lnk/documents (whose associated shell icon is extracted).
static bool IconNameIsPath(const std::wstring& s)
{
    return s.find(L'\\') != std::wstring::npos || s.find(L'/') != std::wstring::npos ||
           s.find(L':')  != std::wstring::npos || HasImageExt(s);
}

// Lazily load an icon as a premultiplied-BGRA D2D bitmap; cache by name (null cached too). An image
// file path loads that image directly and is drawn in full colour; an exe/lnk/document path extracts
// its associated shell icon. Bare names (no path) resolve to nothing (the bundled glyph set is gone).
static IconTex LoadIcon(const wchar_t* name)
{
    std::wstring key(name);
    auto it = gIconCache.find(key);
    if (it != gIconCache.end()) return it->second;

    IconTex out;
    const bool isPath    = IconNameIsPath(key);
    const bool shellIcon = isPath && !HasImageExt(key);   // exe/lnk/document: pull its associated icon
    if (gWIC && gD2DCtx && shellIcon)
    {
        out.colour = true;                                // extracted icons draw in full colour, no tint
        SHFILEINFOW sfi = {};
        if (SHGetFileInfoW(key.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon)
        {
            IWICBitmap* wb = nullptr;
            if (SUCCEEDED(gWIC->CreateBitmapFromHICON(sfi.hIcon, &wb)))
            {
                IWICFormatConverter* conv = nullptr;
                if (SUCCEEDED(gWIC->CreateFormatConverter(&conv)))
                {
                    if (SUCCEEDED(conv->Initialize(wb, GUID_WICPixelFormat32bppPBGRA,
                            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut)))
                        gD2DCtx->CreateBitmapFromWicBitmap(conv, nullptr, &out.bmp);
                    conv->Release();
                }
                wb->Release();
            }
            DestroyIcon(sfi.hIcon);
        }
    }
    else if (gWIC && gD2DCtx && isPath)
    {
        std::wstring path = key;
        out.colour = true;
        IWICBitmapDecoder* dec = nullptr;
        if (SUCCEEDED(gWIC->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, &dec)))
        {
            IWICBitmapFrameDecode* frame = nullptr;
            if (SUCCEEDED(dec->GetFrame(0, &frame)))
            {
                IWICFormatConverter* conv = nullptr;
                if (SUCCEEDED(gWIC->CreateFormatConverter(&conv)))
                {
                    if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut)))
                    {
                        gD2DCtx->CreateBitmapFromWicBitmap(conv, nullptr, &out.bmp);
                    }
                    conv->Release();
                }
                frame->Release();
            }
            dec->Release();
        }
    }
    gIconCache[key] = out; // cache even null (missing file) to avoid re-probing
    return out;
}

// Draw an icon crisply. White (default) icons go through DrawBitmap with high-quality cubic
// downscale (the 256px source -> ~50px was pixelated under FillOpacityMask's linear sampling).
// A soft dark shadow (icon alpha as mask) lifts the glyph off the busy ribbon. Inverted (black)
// icons are still tinted via FillOpacityMask.
static void DrawIcon(const IconTex& t, float cx, float cy, float size, float alpha)
{
    if (!t.bmp) return;
    size *= t.norm;                        // normalise so glyphs read as a consistent size
    float h = size * 0.5f;
    D2D1_RECT_F r = D2D1::RectF(cx - h, cy - h, cx + h, cy + h);

    // Full-colour user image: fit within the square icon box, preserve aspect ratio, centred. No tint.
    if (t.colour)
    {
        D2D1_SIZE_F bs = t.bmp->GetSize();
        float bw = bs.width, bh = bs.height, sc = 1.0f;
        if (bw > 0 && bh > 0) sc = (bw > bh) ? (size / bw) : (size / bh);
        float dw = bw * sc, dh = bh * sc;
        D2D1_RECT_F rc = D2D1::RectF(cx - dw * 0.5f, cy - dh * 0.5f, cx + dw * 0.5f, cy + dh * 0.5f);
        gD2DCtx->DrawBitmap(t.bmp, &rc, alpha, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        return;
    }

    if (gIconBrush)
    {
        D2D1_ANTIALIAS_MODE prev = gD2DCtx->GetAntialiasMode();
        gD2DCtx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        // drop shadow (GFX_SHADOW_ALPHA * 0.75), offset = xmb shadow_offset
        float so = gShadowOffset;
        D2D1_RECT_F sh = D2D1::RectF(r.left + so, r.top + so, r.right + so, r.bottom + so);
        gIconBrush->SetColor(D2D1::ColorF(0, 0, 0));
        gIconBrush->SetOpacity(0.75f * alpha);
        gD2DCtx->FillOpacityMask(t.bmp, gIconBrush, &sh, nullptr);
        if (gIconInverted) // black icon body via tinted mask
        {
            gIconBrush->SetOpacity(alpha);
            gD2DCtx->FillOpacityMask(t.bmp, gIconBrush, &r, nullptr);
        }
        gD2DCtx->SetAntialiasMode(prev);
    }
    if (!gIconInverted) // white icon body, crisp cubic downscale
        gD2DCtx->DrawBitmap(t.bmp, &r, alpha, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
}

// Draw a "+" add-slot marker where a real icon would sit (replaces the old bundled "menu_add" glyph).
// Two rounded white bars with the same soft dark shadow the icons use, sized to match a glyph.
static void DrawPlusGlyph(float cx, float cy, float size, float alpha)
{
    if (!gD2DCtx || !gIconBrush) return;
    const float s    = size * 0.72f;      // match glyph coverage of the slot
    const float half = s * 0.5f;
    const float th   = s * 0.16f;         // half bar thickness
    const float rad  = th;                // rounded bar ends
    const float so   = gShadowOffset;
    auto bars = [&](float dx, float dy)
    {
        D2D1_ROUNDED_RECT hb = { D2D1::RectF(cx - half + dx, cy - th + dy, cx + half + dx, cy + th + dy), rad, rad };
        D2D1_ROUNDED_RECT vb = { D2D1::RectF(cx - th + dx, cy - half + dy, cx + th + dx, cy + half + dy), rad, rad };
        gD2DCtx->FillRoundedRectangle(hb, gIconBrush);
        gD2DCtx->FillRoundedRectangle(vb, gIconBrush);
    };
    gIconBrush->SetColor(D2D1::ColorF(0, 0, 0));   // shadow
    gIconBrush->SetOpacity(0.75f * alpha);
    bars(so, so);
    gIconBrush->SetColor(D2D1::ColorF(1, 1, 1));   // white body
    gIconBrush->SetOpacity(alpha);
    bars(0.0f, 0.0f);
}

// Draw an "app grid" placeholder glyph (four rounded squares) in code - no asset files. Used as the
// default icon for the fresh-install category so its icon slot is not empty. Same white-body + soft
// shadow styling as the other drawn glyphs.
static void DrawAppGridGlyph(float cx, float cy, float size, float alpha)
{
    if (!gD2DCtx || !gIconBrush) return;
    const float s    = size * 0.72f;        // match glyph coverage of the slot
    const float half = s * 0.5f;
    const float gap  = s * 0.16f;           // space between the squares
    const float sq   = (s - gap) * 0.5f;    // side of each square
    const float rad  = sq * 0.28f;          // rounded corners
    const float so   = gShadowOffset;
    auto squares = [&](float dx, float dy)
    {
        const float L0 = cx - half, R1 = cx + half, T0 = cy - half, B1 = cy + half;
        D2D1_ROUNDED_RECT q[4] = {
            { D2D1::RectF(L0 + dx,      T0 + dy,      L0 + sq + dx, T0 + sq + dy), rad, rad },
            { D2D1::RectF(R1 - sq + dx, T0 + dy,      R1 + dx,      T0 + sq + dy), rad, rad },
            { D2D1::RectF(L0 + dx,      B1 - sq + dy, L0 + sq + dx, B1 + dy),      rad, rad },
            { D2D1::RectF(R1 - sq + dx, B1 - sq + dy, R1 + dx,      B1 + dy),      rad, rad },
        };
        for (auto& r : q) gD2DCtx->FillRoundedRectangle(r, gIconBrush);
    };
    gIconBrush->SetColor(D2D1::ColorF(0, 0, 0));   // shadow
    gIconBrush->SetOpacity(0.75f * alpha);
    squares(so, so);
    gIconBrush->SetColor(D2D1::ColorF(1, 1, 1));   // white body
    gIconBrush->SetOpacity(alpha);
    squares(0.0f, 0.0f);
}

// Draw a menu entry's icon from its stored string. Reserved sentinels draw a code vector glyph (no
// asset files): "menu_add" = the edit-mode "+" add slot, "apps" = the app-grid placeholder. Anything
// else is a file path loaded via LoadIcon (user image / extracted exe icon). Empty = draw nothing.
static void DrawEntryIcon(const std::wstring& icon, float cx, float cy, float size, float alpha)
{
    if (icon.empty()) return;
    if (icon == L"menu_add") { DrawPlusGlyph(cx, cy, size, alpha);    return; }
    if (icon == L"apps")     { DrawAppGridGlyph(cx, cy, size, alpha); return; }
    DrawIcon(LoadIcon(icon.c_str()), cx, cy, size, alpha);
}

static void CreateMenuFormats()
{
    if (!gDWrite) return;
    MenuLayout L = ComputeLayout(gW, gH);
    if (gItemFmt) { gItemFmt->Release(); gItemFmt = nullptr; }
    if (gCatFmt)  { gCatFmt->Release();  gCatFmt  = nullptr; }
    if (gItemTitleFmt) { gItemTitleFmt->Release(); gItemTitleFmt = nullptr; }
    if (gCatTitleFmt)  { gCatTitleFmt->Release();  gCatTitleFmt  = nullptr; }
    if (gClockTimeFmt) { gClockTimeFmt->Release(); gClockTimeFmt = nullptr; }
    if (gClockDateFmt) { gClockDateFmt->Release(); gClockDateFmt = nullptr; }
    if (gSubFmt)       { gSubFmt->Release();       gSubFmt       = nullptr; }
    const wchar_t* font = UiFont();
    gDWrite->CreateTextFormat(font, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, L.fontSize, L"en-us", &gItemFmt);
    // gCatFmt is the active-category TITLE (XMB shows only the selected category name, big, top-left).
    gDWrite->CreateTextFormat(font, nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, L.fontSize * 1.6f, L"en-us", &gCatFmt);
    // Clock HUD: big time line + smaller date line beneath it. Both scale by the user clock size;
    // each line's weight follows its bold toggle (time: Bold/Light, date: Bold/Semi-bold).
    gDWrite->CreateTextFormat(font, nullptr, gClockBold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_LIGHT,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, L.fontSize * 1.8f * gClockScale, L"en-us", &gClockTimeFmt);
    gDWrite->CreateTextFormat(font, nullptr, gDateBold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, L.fontSize * 0.75f * gClockScale, L"en-us", &gClockDateFmt);
    // gSubFmt: small format reused for the Settings row descriptions + editor hint line.
    { float f2 = 22.0f * L.scale; if (f2 < 6.0f) f2 = 6.0f;
      gDWrite->CreateTextFormat(font, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, f2, L"en-us", &gSubFmt); }
    // gItemTitleFmt: real menu-item labels, size scaled by the current layout's itemTitleScale.
    { float itF = L.fontSize * CurTune().itemTitleScale; if (itF < 6.0f) itF = 6.0f;
      gDWrite->CreateTextFormat(font, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, itF, L"en-us", &gItemTitleFmt); }
    // gCatTitleFmt: the active-category title, size scaled by the current layout's catTitleScale.
    { float ctF = L.fontSize * CurTune().catTitleScale; if (ctF < 6.0f) ctF = 6.0f;
      gDWrite->CreateTextFormat(font, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, ctF, L"en-us", &gCatTitleFmt); }
    if (gItemFmt) gItemFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); // vert-center in row rect
    if (gCatFmt)  gCatFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);  // left-aligned by default
    if (gItemTitleFmt) gItemTitleFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (gCatTitleFmt)  { gCatTitleFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                         gCatTitleFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
}
