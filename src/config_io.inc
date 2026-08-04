// --- config (JSON) loading (Phase 3) ---
static std::wstring gConfigPath;      // resolved path to launcher.json
static std::wstring gConfigError;     // last load/parse error (surfaced in the menu on failure)

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// Starter config written on first run so launching is testable out of the box. Uses bare command
// names (resolved via PATH by CreateProcess) + one URL (opened by the shell). No backslashes, so it
// stays valid JSON without escaping; users edit this file to add their own apps.
static const char* kDefaultConfig = u8R"JSON({
  "categories": [
    { "title": "New", "icon": "apps", "items": [] }
  ],
  "menu": { "orientation": "horizontal", "placement": "scrolling", "clockScale": 1.0, "scale": 1.0, "font": "Segoe UI", "enlargeSelected": true },
  "clock": { "boldTime": false, "boldDate": false, "showDate": true, "blinkColon": true, "hourFormat": "follow", "dateFormat": 4, "numberSep": ".", "clockCorner": "topRight" },
  "appearance": { "background": "Ribbon", "ribbon": "Custom Colour Ribbon", "theme": "Dark", "ribbonCycle": false },
  "screensaver": { "enabled": true, "timeoutSeconds": 30, "dimEnabled": true, "dimPercent": 50, "clock": { "enabled": true, "motion": "clockwise" } },
  "keepDisplayAwake": true,
  "returnHome": true,
  "kioskShellBackup": ""
})JSON";

// launcher.json lives next to the exe - ONLY the exe's own folder is checked (no walk-up). If it is
// not there it is created there on first run.
static void ResolveConfigPath()
{
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    size_t p = dir.find_last_of(L"\\/");
    if (p != std::wstring::npos) dir = dir.substr(0, p + 1);
    gConfigPath = dir + L"launcher.json";
}

static bool ReadFileUtf8(const std::wstring& path, std::string& out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz = {};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (1 << 24)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    BOOL ok = ReadFile(h, out.empty() ? nullptr : &out[0], (DWORD)out.size(), &rd, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(rd);
    return true;
}

static bool WriteFileUtf8(const std::wstring& path, const std::string& data)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

// Load gConfigPath into gMenu. Creates the default config if the file is missing. On any error,
// sets gConfigError and returns false (the caller falls back to a placeholder so the app still runs).
static bool LoadConfig()
{
    gConfigError.clear();
    // Fixed-placement anchor defaults to Center (struct default alignH/alignV = 1) for both orientations.
    std::string bytes;
    if (!ReadFileUtf8(gConfigPath, bytes))
    {
        WriteFileUtf8(gConfigPath, kDefaultConfig);   // first run: seed a starter config
        gFirstRun = true;                             // no config existed: drop into edit mode so the user builds their own menu
        if (!ReadFileUtf8(gConfigPath, bytes)) { gConfigError = L"cannot read launcher.json"; return false; }
    }
    std::string err;
    json::Value root = json::parse(bytes, err);
    if (!err.empty()) { gConfigError = Utf8ToWide(err); return false; }
    const json::Value* cats = root.find("categories");
    if (!cats || !cats->isArray()) { gConfigError = L"launcher.json: 'categories' array missing"; return false; }

    std::vector<MenuCategory> menu;
    for (const json::Value& c : cats->arr)
    {
        if (!c.isObject()) continue;
        MenuCategory cat;
        cat.title = Utf8ToWide(c.getStr("title"));
        cat.icon  = Utf8ToWide(c.getStr("icon"));
        cat.isItem    = c.getBool("isItem", false);           // top-level direct-launch item (no sub-menu)
        cat.target    = Utf8ToWide(c.getStr("target"));
        cat.arguments = Utf8ToWide(c.getStr("arguments"));
        cat.waitForExit = c.getBool("waitForExit", true);     // top-level item launch flag (isItem only)
        cat.trackWindow = c.getBool("trackWindow", false);    // window-watch launch mode (isItem only)
        cat.processName = Utf8ToWide(c.getStr("processName", ""));
        const json::Value* items = c.find("items");
        if (items && items->isArray())
            for (const json::Value& iv : items->arr)
            {
                if (!iv.isObject()) continue;
                MenuItem it;
                it.type            = Utf8ToWide(iv.getStr("type", "app"));
                it.title           = Utf8ToWide(iv.getStr("title"));
                it.icon            = Utf8ToWide(iv.getStr("icon"));
                it.target          = Utf8ToWide(iv.getStr("target"));
                it.arguments       = Utf8ToWide(iv.getStr("arguments"));
                it.waitForExit     = iv.getBool("waitForExit", true);
                it.trackWindow     = iv.getBool("trackWindow", false);
                it.processName     = Utf8ToWide(iv.getStr("processName", ""));
                cat.items.push_back(std::move(it));
            }
        menu.push_back(std::move(cat));
    }
    if (menu.empty()) { gConfigError = L"launcher.json: no categories defined"; return false; }
    gMenu = std::move(menu);

    // Optional "menu" settings: orientation, placement, clock size. Missing -> keep defaults.
    if (const json::Value* m = root.find("menu"))
    {
        gVertical    = (m->getStr("orientation", "horizontal") == "vertical");
        gFixedLayout = (m->getStr("placement",   "scrolling")  == "fixed");
        double cs = m->getNum("clockScale", 1.0);
        if (cs < 0.4) cs = 0.4; if (cs > 3.0) cs = 3.0;
        gClockScale = (float)cs;
        double ms = m->getNum("scale", 1.0);            // menu-wide scale
        if (ms < 0.50) ms = 0.50; if (ms > 2.0) ms = 2.0;
        gMenuScale = (float)ms;
        gEnlargeSelected = m->getBool("enlargeSelected", true);
        gMenuPosX = (float)m->getNum("posX", 0.0);   // Part C: whole-menu screen offset
        gMenuPosY = (float)m->getNum("posY", 0.0);
        // Layout-tuning knobs, stored per (orientation x placement) under "menu.layouts". Multipliers
        // clamped 0.10-2.0; default 1.0 = classic look. Missing keys keep the defaults.
        if (const json::Value* lay = m->find("layouts"))
        {
            auto clampMul = [](double v){ if (v < 0.10) v = 0.10; if (v > 2.0) v = 2.0; return (float)v; };
            const char* keys[4] = { "horizontalScrolling", "horizontalFixed", "verticalScrolling", "verticalFixed" };
            for (int k = 0; k < 4; ++k)
                if (const json::Value* t = lay->find(keys[k]))
                {
                    LayoutTune& u = gTune[k];
                    u.spItem         = clampMul(t->getNum("itemSpacing",   1.0));
                    u.focusGap       = clampMul(t->getNum("focusGap",      1.0));
                    u.spCatItem      = clampMul(t->getNum("catItemGap",    1.0));
                    u.spCat          = clampMul(t->getNum("catSpacing",    1.0));
                    u.spIconTitle    = clampMul(t->getNum("iconTitleGap",  1.0));
                    u.catIconTitle   = clampMul(t->getNum("catIconTitleGap", 1.0));
                    u.itemTitleScale = clampMul(t->getNum("itemTitleSize", 1.0));
                    u.catTitleScale  = clampMul(t->getNum("catTitleSize",  1.0));
                    u.catIconScale   = clampMul(t->getNum("catIconSize",   1.0));
                    u.itemIconScale  = clampMul(t->getNum("itemIconSize",  1.0));
                    u.itemTitleShow  = t->getBool("itemTitleShow", true);
                    u.catTitleShow   = t->getBool("catTitleShow",  true);
                    u.catTitleAll    = t->getBool("catTitleAll",   false);   // show every category title
                    u.subReverse     = t->getBool("reverse", false);   // Part C2: reversed submenu flow
                    { int la = (int)t->getNum("listAnim", 1);          // per-mode list-switch animation: 0 None/1 Slide/2 Fade/3 Axis slide
                      u.listAnim = (la < 0 || la > 3) ? 1 : la; }

                    { auto clampAlign = [](double v){ int i=(int)(v+0.5); return i<0?0:i>2?2:i; };
                      u.alignH = clampAlign(t->getNum("alignH", (double)u.alignH));   // per-mode defaults above
                      u.alignV = clampAlign(t->getNum("alignV", (double)u.alignV)); }
                }
        }
        std::string fn = m->getStr("font", "");
        if (!fn.empty()) { std::wstring wf = Utf8ToWide(fn);
            for (int i = 0; i < kUiFontCount; ++i) if (wf == kUiFonts[i]) { gUiFontIdx = i; break; } }
    }

    // Optional "clock" appearance: bold time/date, show date, hour + date format presets.
    if (const json::Value* ck = root.find("clock"))
    {
        gClockBold   = ck->getBool("boldTime", false);
        gDateBold    = ck->getBool("boldDate", false);
        gDateEnabled = ck->getBool("showDate", true);
        gBlinkColon  = ck->getBool("blinkColon", true);
        std::string hf = ck->getStr("hourFormat", "follow");
        gHourFormat = (hf == "12") ? 1 : (hf == "24") ? 2 : (hf == "12en") ? 3 : 0;
        int df = (int)ck->getNum("dateFormat", 4.0);
        if (df < 0) df = 0; if (df > 12) df = 12;   // 13 date formats (0-12)
        gDateFormat = df;
        std::string sep = ck->getStr("numberSep", ".");
        gDateSep = (sep == "-") ? 1 : (sep == "/") ? 2 : 0;
        std::string cc = ck->getStr("clockCorner", "topRight");
        gClockCorner = (cc == "topLeft") ? CLOCK_TL : (cc == "bottomRight") ? CLOCK_BR
                     : (cc == "bottomLeft") ? CLOCK_BL : CLOCK_TR;
    }

    // Optional "appearance": background ("None" / "Ribbon" / an animated-background name), the
    // remembered ribbon variant (by name), and theme (by name). Resolved against the pipeline/theme
    // lists built in InitPipelines (which runs before LoadConfig). Unknown names keep the defaults.
    if (const json::Value* ap = root.find("appearance"))
    {
        std::string bg = ap->getStr("background", "");
        std::string rb = ap->getStr("ribbon", "");
        std::string th = ap->getStr("theme", "");
        if (!rb.empty())                                   // remembered ribbon variant (never "None")
            for (int i = 0; i < (int)gRibbonList.size(); ++i)
                if (gPipes[gRibbonList[i]].name && rb == gPipes[gRibbonList[i]].name) { gRibbonChoice = i; break; }
        if (!bg.empty())
        {
            if (bg == "None" || bg == "none")      gBgChoice = 0;
            else if (bg == "Ribbon" || bg == "ribbon") gBgChoice = BgRibbonChoice();
            else if (bg == "Image" || bg == "image")   gBgChoice = BgImageChoice();
            else for (int i = 0; i < (int)gBgList.size(); ++i)
                if (gPipes[gBgList[i]].name && bg == gPipes[gBgList[i]].name) { gBgChoice = i + 1; break; }
        }
        if (th == "Custom" || th == "custom") gTheme = kThemeCount;
        else if (!th.empty())
            for (int i = 0; i < kThemeCount; ++i)
                if (th == kThemes[i].name) { gTheme = i; break; }
        std::string bgi = ap->getStr("bgImage", "");
        if (!bgi.empty()) gBgImagePath = Utf8ToWide(bgi);
        // Colours: [r,g,b] arrays, 0-255. Missing -> keep defaults.
        auto readRGB = [&](const char* key, float* out)
        {
            const json::Value* a = ap->find(key);
            if (a && a->isArray() && (int)a->arr.size() >= 3)
                for (int k = 0; k < 3; ++k) { double v = a->arr[k].asNumber(out[k] * 255.0); out[k] = (float)(v / 255.0); }
        };
        float rc[3] = { gRibbonColor.x, gRibbonColor.y, gRibbonColor.z };
        readRGB("ribbonColor", rc); gRibbonColor.x = rc[0]; gRibbonColor.y = rc[1]; gRibbonColor.z = rc[2];
        gRibbonOpacity = (float)ap->getNum("ribbonOpacity", 1.0);   // default unchanged (1.0 = full)
        if (gRibbonOpacity < 0.05f) gRibbonOpacity = 0.05f; if (gRibbonOpacity > 1.0f) gRibbonOpacity = 1.0f;
        gRibbonSheen = (float)ap->getNum("ribbonSheen", 13.0);      // fold-sheen divisor (shader default /13)
        if (gRibbonSheen < 1.0f) gRibbonSheen = 1.0f; if (gRibbonSheen > 20.0f) gRibbonSheen = 20.0f;
        gRibbonCycle = ap->getBool("ribbonCycle", false);           // slow continuous hue cycling
        readRGB("themeTop", gThemeTop);
        readRGB("themeBottom", gThemeBot);
        BuildCustomTheme();                                // reflect loaded custom-theme colours
        ApplyBgSelection();                                // derive gBgSel / gRibbonSel from the choice
    }

    // Optional "screensaver" settings (Phase 4). Missing -> keep defaults.
    if (const json::Value* ss = root.find("screensaver"))
    {
        gScreensaverEnabled = ss->getBool("enabled", true);
        double secs = ss->getNum("timeoutSeconds", 30.0);
        if (secs < 5.0) secs = 5.0;                       // floor so it can't get stuck instantly (5s grid)
        gIdleTimeoutMs = (float)(secs * 1000.0);
        gDimEnabled = ss->getBool("dimEnabled", true);
        int dp = (int)ss->getNum("dimPercent", 50.0);
        if (dp < 0) dp = 0; if (dp > 100) dp = 100;
        gSaverDimPercent = dp;
        // "clock" is an object { enabled, motion } now; tolerate the legacy number form (0 = off).
        if (const json::Value* clk = ss->find("clock"))
        {
            if (clk->isObject())
            {
                gSaverClockEnabled = clk->getBool("enabled", true);
                std::string mo = clk->getStr("motion", "clockwise");
                gSaverClockMotion = (mo == "counterclockwise" || mo == "ccw") ? 1
                                  : (mo == "random")                          ? 2 : 0;
            }
            else if (clk->isNumber())
                gSaverClockEnabled = (clk->asNumber(1.0) != 0.0);
        }
    }

    // Keep display/system awake while foreground (inhibit OS screensaver + sleep). Default on.
    gInhibitSleep = root.getBool("keepDisplayAwake", true);
    gReturnHome   = root.getBool("returnHome", true);
    // Kiosk: the Winlogon Shell value that was in place before the launcher took over (empty = restore to
    // the machine default). Autostart + kiosk on/off are read live from the registry, not stored here.
    gShellBackup  = Utf8ToWide(root.getStr("kioskShellBackup", ""));
    return true;
}

// --- config saving (Phase 5: edit mode persists changes) ---
static bool gConfigLoaded = false;    // false = running on the fallback placeholder (don't overwrite)

static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// Escape a wide string as a JSON string body (UTF-8 out; quotes/backslash/control chars escaped).
static std::string JsonEsc(const std::wstring& w)
{
    std::string s = WideToUtf8(w), o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else o += (char)c;
        }
    }
    return o;
}

// Serialise gMenu + screensaver settings back to launcher.json. Refuses while on the fallback
// placeholder (a broken config) so a parse error is never silently overwritten.
static bool SaveConfig()
{
    if (!gConfigLoaded) return false;
    std::string o = "{\n  \"categories\": [\n";
    for (size_t ci = 0; ci < gMenu.size(); ++ci)
    {
        const MenuCategory& c = gMenu[ci];
        o += "    {\n";
        o += "      \"title\": \"" + JsonEsc(c.title) + "\",\n";
        o += "      \"icon\": \""  + JsonEsc(c.icon)  + "\",\n";
        if (c.isItem)
        {
            o += "      \"isItem\": true,\n";
            if (!c.target.empty())    o += "      \"target\": \""    + JsonEsc(c.target)    + "\",\n";
            if (!c.arguments.empty()) o += "      \"arguments\": \"" + JsonEsc(c.arguments) + "\",\n";
            o += std::string("      \"waitForExit\": ") + (c.waitForExit ? "true" : "false") + ",\n";
            if (c.trackWindow)         o += "      \"trackWindow\": true,\n";
            if (!c.processName.empty())o += "      \"processName\": \"" + JsonEsc(c.processName) + "\",\n";
        }
        o += "      \"items\": [";
        if (c.items.empty()) o += "]\n";
        else
        {
            o += "\n";
            for (size_t ii = 0; ii < c.items.size(); ++ii)
            {
                const MenuItem& it = c.items[ii];
                std::string f;
                auto addS = [&](const char* k, const std::wstring& v) {
                    if (v.empty()) return;
                    if (!f.empty()) f += ", ";
                    f += std::string("\"") + k + "\": \"" + JsonEsc(v) + "\"";
                };
                auto addB = [&](const char* k, bool v) {
                    if (!f.empty()) f += ", ";
                    f += std::string("\"") + k + "\": " + (v ? "true" : "false");
                };
                addS("title", it.title); addS("icon", it.icon);
                addS("target", it.target); addS("arguments", it.arguments);
                addB("waitForExit", it.waitForExit);
                if (it.trackWindow) addB("trackWindow", true);
                addS("processName", it.processName);
                o += "        {" + f + (ii + 1 < c.items.size() ? "},\n" : "}\n");
            }
            o += "      ]\n";
        }
        o += (ci + 1 < gMenu.size()) ? "    },\n" : "    }\n";
    }
    o += "  ],\n";
    {
        char mb[480];
        snprintf(mb, sizeof(mb),
                 "  \"menu\": { \"orientation\": \"%s\", \"placement\": \"%s\", \"clockScale\": %.3f, \"scale\": %.3f, \"font\": \"%s\", \"enlargeSelected\": %s, \"posX\": %.1f, \"posY\": %.1f,\n    \"layouts\": {\n",
                 gVertical ? "vertical" : "horizontal",
                 gFixedLayout ? "fixed" : "scrolling", (double)gClockScale, (double)gMenuScale,
                 WideToUtf8(UiFont()).c_str(), gEnlargeSelected ? "true" : "false",
                 (double)gMenuPosX, (double)gMenuPosY);
        o += mb;
        const char* keys[4] = { "horizontalScrolling", "horizontalFixed", "verticalScrolling", "verticalFixed" };
        for (int k = 0; k < 4; ++k)
        {
            const LayoutTune& u = gTune[k];
            char tb[832];
            snprintf(tb, sizeof(tb),
                     "      \"%s\": { \"itemSpacing\": %.2f, \"focusGap\": %.2f, \"catItemGap\": %.2f, \"catSpacing\": %.2f, \"iconTitleGap\": %.2f, \"catIconTitleGap\": %.2f, "
                     "\"itemTitleSize\": %.2f, \"itemTitleShow\": %s, \"catTitleSize\": %.2f, \"catTitleShow\": %s, \"catTitleAll\": %s, \"catIconSize\": %.2f, \"itemIconSize\": %.2f, \"alignH\": %d, \"alignV\": %d, \"reverse\": %s, \"listAnim\": %d }%s\n",
                     keys[k], (double)u.spItem, (double)u.focusGap, (double)u.spCatItem, (double)u.spCat, (double)u.spIconTitle, (double)u.catIconTitle,
                     (double)u.itemTitleScale, u.itemTitleShow ? "true" : "false",
                     (double)u.catTitleScale, u.catTitleShow ? "true" : "false", u.catTitleAll ? "true" : "false",
                     (double)u.catIconScale, (double)u.itemIconScale, u.alignH, u.alignV, u.subReverse ? "true" : "false", u.listAnim, (k < 3 ? "," : ""));
            o += tb;
        }
        o += "    }\n  },\n";
    }
    const char* hourFmt = (gHourFormat == 1) ? "12" : (gHourFormat == 2) ? "24" : (gHourFormat == 3) ? "12en" : "follow";
    const char* numSep  = (gDateSep == 1) ? "-" : (gDateSep == 2) ? "/" : ".";
    const char* clkCorner = (gClockCorner == CLOCK_TL) ? "topLeft" : (gClockCorner == CLOCK_BR) ? "bottomRight"
                          : (gClockCorner == CLOCK_BL) ? "bottomLeft" : "topRight";
    char cb[320];
    snprintf(cb, sizeof(cb),
             "  \"clock\": { \"boldTime\": %s, \"boldDate\": %s, \"showDate\": %s, \"blinkColon\": %s, \"hourFormat\": \"%s\", \"dateFormat\": %d, \"numberSep\": \"%s\", \"clockCorner\": \"%s\" },\n",
             gClockBold ? "true" : "false", gDateBold ? "true" : "false", gDateEnabled ? "true" : "false",
             gBlinkColon ? "true" : "false", hourFmt, gDateFormat, numSep, clkCorner);
    o += cb;
    // Appearance: background is "None" / "Ribbon" / an animated-background name; ribbon is the
    // remembered variant (always a real ribbon); theme by name. Names are simple ASCII.
    const char* bgName = (gBgChoice == 0)                ? "None"
                       : (gBgChoice == BgRibbonChoice()) ? "Ribbon"
                       : (gBgChoice == BgImageChoice())  ? "Image"
                       : gPipes[gBgList[gBgChoice - 1]].name;
    const char* rbName = (gRibbonChoice >= 0 && gRibbonChoice < (int)gRibbonList.size())
                       ? gPipes[gRibbonList[gRibbonChoice]].name : "Custom Colour Ribbon";
    const char* thName = (gTheme == kThemeCount) ? "Custom"
                       : (gTheme >= 0 && gTheme < kThemeCount) ? kThemes[gTheme].name : "Dark";
    auto ci = [](float c) { return std::to_string(Ch255(c)); };
    char rob[32]; snprintf(rob, sizeof(rob), "%.2f", (double)gRibbonOpacity);
    char rsb[32]; snprintf(rsb, sizeof(rsb), "%.2f", (double)gRibbonSheen);
    o += std::string("  \"appearance\": { \"background\": \"") + (bgName ? bgName : "None")
       + "\", \"ribbon\": \"" + (rbName ? rbName : "Custom Colour Ribbon")
       + "\", \"theme\": \""  + (thName ? thName : "Dark")
       + "\", \"bgImage\": \"" + JsonEsc(gBgImagePath) + "\""
       + ", \"ribbonColor\": [" + ci(gRibbonColor.x) + "," + ci(gRibbonColor.y) + "," + ci(gRibbonColor.z) + "]"
       + ", \"ribbonCycle\": " + (gRibbonCycle ? "true" : "false")
       + ", \"ribbonOpacity\": " + rob
       + ", \"ribbonSheen\": " + rsb
       + ", \"themeTop\": ["    + ci(gThemeTop[0]) + "," + ci(gThemeTop[1]) + "," + ci(gThemeTop[2]) + "]"
       + ", \"themeBottom\": [" + ci(gThemeBot[0]) + "," + ci(gThemeBot[1]) + "," + ci(gThemeBot[2]) + "]"
       + " },\n";
    const char* motion = gSaverClockMotion == 1 ? "counterclockwise"
                       : gSaverClockMotion == 2 ? "random" : "clockwise";
    char ss[320];
    snprintf(ss, sizeof(ss),
             "  \"screensaver\": { \"enabled\": %s, \"timeoutSeconds\": %d, \"dimEnabled\": %s, \"dimPercent\": %d, \"clock\": { \"enabled\": %s, \"motion\": \"%s\" } },\n",
             gScreensaverEnabled ? "true" : "false",
             (int)(gIdleTimeoutMs / 1000.0f + 0.5f), gDimEnabled ? "true" : "false", gSaverDimPercent,
             gSaverClockEnabled ? "true" : "false", motion);
    o += ss;
    o += std::string("  \"keepDisplayAwake\": ") + (gInhibitSleep ? "true" : "false") + ",\n";
    o += std::string("  \"returnHome\": ") + (gReturnHome ? "true" : "false") + ",\n";
    o += std::string("  \"kioskShellBackup\": \"") + JsonEsc(gShellBackup) + "\"\n";
    o += "}\n";
    return WriteFileUtf8(gConfigPath, o);
}

static void BuildMenu()
{
    ResolveConfigPath();
    gConfigLoaded = LoadConfig();
    if (!gConfigLoaded)
    {
        // Fallback so the app still renders and shows what went wrong.
        gMenu.clear();
        MenuCategory c; c.title = L"Launcher";   // no icon (bundled glyph set removed)
        MenuItem e;
        e.title = gConfigError.empty() ? L"Config error" : gConfigError;
        c.items.push_back(e);
        gMenu.push_back(std::move(c));
    }
    // Settings is a popup (UI_SETTINGS), not a menu category - the menu shows only the user's own
    // categories/items.
    gItemSel.assign(gMenu.size(), 0);
    gCatSel = 0;
    gCatTween.set(0.0f);
    gListFade.set(1.0f);        // list visible at startup (no switch animation yet)
    RetargetItems(true);
}
