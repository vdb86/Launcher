// =============================================================================
// Phase 5: in-engine, controller-first edit mode (no mouse dialogs).
//   UI_BROWSE   - normal menu navigation + launching.
//   UI_ACTION   - action list overlay operating on the current category/item.
//   UI_KEYBOARD - on-screen keyboard capturing text into a field (hardware kbd also works).
//   UI_FILES    - file browser picking an .exe path.
// Every edit mutates gMenu and is persisted via SaveConfig().
// (UiMode + gUi are declared up near the menu globals so DrawMenu can read them.)
// =============================================================================

// Which field a keyboard/browse session writes back to on confirm.
enum EditField {
    EF_NONE, EF_ITEM_TITLE, EF_ITEM_EXE, EF_ITEM_ARGS, EF_ITEM_ICON,
    EF_CAT_TITLE, EF_CAT_ICON, EF_CAT_EXE, EF_BG_IMAGE
};
static EditField gEditField = EF_NONE;

// The context popup (UI_ACTION) has two pages: a top-level context menu for the currently selected
// item/category, and a field editor opened by "Edit item..." / "Edit category...". The popup always
// operates on CurItem()/CurCat() - i.e. the item the user just navigated to and can see behind it.
enum ActPage { AP_CTX, AP_ITEM_FIELDS, AP_CAT_FIELDS, AP_TOPITEM_FIELDS, AP_ADDCHOICE };
static ActPage gActPage = AP_CTX;
static int     gActionSel = 0;

// Shared label for the per-item "window-watch" toggle row (item + top-level-item field pages). Its
// On/Off state is appended when drawn (see DrawEditOverlay); ActionActivate flips it. ON = for single-
// instance / tray apps: let the app come forward and return to the launcher when its window closes/hides.
static const wchar_t* const kRowTrackWindow = L"Track window (tray apps)";

// Context page depends on what is focused: an item or the category tab. "Move" enters a live move
// mode (repositions with the D-pad); the old separate Move up / Move down entries are gone.
static const wchar_t* kItemCtx[] = {
    L"Edit item...", L"Move", L"Delete item", L"Close",   // adding is done via the top-menu "+" chooser
};
static const wchar_t* kCatCtx[] = {
    L"Edit category...", L"Move", L"Delete category", L"Add item", L"Add category", L"Close",
};
// Context for a TOP-LEVEL ITEM (a launcher tab with no sub-menu). Same shape as kItemCtx but its
// actions operate on the tab itself (edit its path/title/icon, delete the whole tab).
static const wchar_t* kTopItemCtx[] = {
    L"Edit item...", L"Move", L"Delete item", L"Close",   // adding is done via the top-menu "+" chooser
};
static const wchar_t* kItemFields[] = {
    L"Path (browse for file)", L"Title", L"Arguments",
    L"Icon (browse)", kRowTrackWindow, L"Back",
};
static const wchar_t* kCatFields[] = { L"Title", L"Icon (browse)", L"Back" };
// Field editor for a top-level item: like an item but stored on the category (no Arguments row for now).
static const wchar_t* kTopItemFields[] = {
    L"Path (browse for file)", L"Title", L"Icon (browse)", kRowTrackWindow, L"Back",
};
// The top-menu "+" opens this chooser: add a category (container) or a top-level item (launcher).
static const wchar_t* kAddChoice[] = { L"Add item", L"Add category", L"Close" };

// True when the focused category tab is a top-level direct-launch item (rather than a container).
static bool FocusCatIsItem()
{
    return gCatSel >= 0 && gCatSel < (int)gMenu.size() && gMenu[gCatSel].isItem;
}

static const wchar_t* const* ActList()
{
    if (gActPage == AP_ITEM_FIELDS)    return kItemFields;
    if (gActPage == AP_CAT_FIELDS)     return kCatFields;
    if (gActPage == AP_TOPITEM_FIELDS) return kTopItemFields;
    if (gActPage == AP_ADDCHOICE)      return kAddChoice;
    if (gEditFocusCat)                 return FocusCatIsItem() ? kTopItemCtx : kCatCtx;
    return kItemCtx;
}
static int ActCount()
{
    if (gActPage == AP_ITEM_FIELDS)    return (int)(sizeof(kItemFields)    / sizeof(kItemFields[0]));
    if (gActPage == AP_CAT_FIELDS)     return (int)(sizeof(kCatFields)     / sizeof(kCatFields[0]));
    if (gActPage == AP_TOPITEM_FIELDS) return (int)(sizeof(kTopItemFields) / sizeof(kTopItemFields[0]));
    if (gActPage == AP_ADDCHOICE)      return (int)(sizeof(kAddChoice)     / sizeof(kAddChoice[0]));
    if (gEditFocusCat)                 return FocusCatIsItem() ? (int)(sizeof(kTopItemCtx) / sizeof(kTopItemCtx[0]))
                                                               : (int)(sizeof(kCatCtx)     / sizeof(kCatCtx[0]));
    return (int)(sizeof(kItemCtx) / sizeof(kItemCtx[0]));
}

// On-screen keyboard state.
static std::wstring gKbText;      // text being edited
static std::wstring gKbPrompt;    // label shown above the keyboard
static int  gKbRow = 0, gKbCol = 0;
static bool gKbUpper = false;
static const wchar_t* kKbRows[4] = { L"1234567890", L"qwertyuiop", L"asdfghjkl:", L"zxcvbnm-_." };
enum KbSpecial { KS_SHIFT, KS_BSLASH, KS_SLASH, KS_SPACE, KS_BACK, KS_CLEAR, KS_OK, KS_CANCEL, KS_COUNT };
static const wchar_t* kKbSpecials[KS_COUNT] = { L"Shift", L"\\", L"/", L"Space", L"Bksp", L"Clear", L"OK", L"Cancel" };

// File browser state.
struct BrowseEntry { std::wstring name; bool isDir; };
static std::wstring             gBrowsePath;    // current dir ("" = drive list)
static std::vector<BrowseEntry> gBrowseEntries;
static int                      gBrowseSel = 0;
static bool                     gBrowseImagesOnly = false;  // icon picker: list only image files
static UiMode                   gFilesReturn = UI_ACTION;   // UI mode to return to when the browser closes

// --- current selection accessors ---
static MenuCategory* CurCat()
{
    return (gCatSel >= 0 && gCatSel < (int)gMenu.size()) ? &gMenu[gCatSel] : nullptr;
}
static MenuItem* CurItem()
{
    MenuCategory* c = CurCat();
    if (!c || gCatSel >= (int)gItemSel.size()) return nullptr;
    int s = gItemSel[gCatSel];
    return (s >= 0 && s < (int)c->items.size()) ? &c->items[s] : nullptr;
}

// --- apply an edited text value to its target field ---
static void ApplyEditField(const std::wstring& v)
{
    MenuCategory* c = CurCat();
    MenuItem* it = CurItem();
    switch (gEditField)
    {
    case EF_ITEM_TITLE:   if (it) it->title = v;       break;
    case EF_ITEM_EXE:     if (it) it->target = v;      break;
    case EF_ITEM_ARGS:    if (it) it->arguments = v;   break;
    case EF_ITEM_ICON:    if (it) it->icon = v;        break;
    case EF_CAT_TITLE:    if (c)  c->title = v;        break;
    case EF_CAT_ICON:     if (c)  c->icon = v;         break;
    case EF_CAT_EXE:      if (c)  c->target = v;       break;   // top-level item launch path
    case EF_BG_IMAGE:     gBgImagePath = v;            break;   // full-screen background image
    default: break;
    }
    SaveConfig();
}

// --- on-screen keyboard control ---
static void KbStart(EditField field, const wchar_t* prompt, const std::wstring& initial)
{
    gEditField = field;
    gKbPrompt  = prompt;
    gKbText    = initial;
    gKbRow = 0; gKbCol = 0; gKbUpper = false;
    gUi = UI_KEYBOARD;
}
static void KbConfirm()   { ApplyEditField(gKbText); gEditField = EF_NONE; gUi = UI_ACTION; }
static void KbCancelEdit(){ gEditField = EF_NONE; gUi = UI_ACTION; }

// Append a typed character (hardware keyboard path, from WM_CHAR).
static void KbChar(wchar_t c)
{
    if (c == L'\b') { if (!gKbText.empty()) gKbText.pop_back(); return; }
    if (c >= L' ')  gKbText.push_back(c);
}

// Activate the highlighted on-screen key (controller path).
static void KbActivate()
{
    if (gKbRow < 4)
    {
        const wchar_t* row = kKbRows[gKbRow];
        int len = (int)wcslen(row);
        if (gKbCol >= 0 && gKbCol < len)
        {
            wchar_t ch = row[gKbCol];
            if (gKbUpper && ch >= L'a' && ch <= L'z') ch = (wchar_t)(ch - L'a' + L'A');
            gKbText.push_back(ch);
        }
        return;
    }
    switch ((KbSpecial)gKbCol)
    {
    case KS_SHIFT:  gKbUpper = !gKbUpper; break;
    case KS_BSLASH: gKbText.push_back(L'\\'); break;
    case KS_SLASH:  gKbText.push_back(L'/'); break;
    case KS_SPACE:  gKbText.push_back(L' '); break;
    case KS_BACK:   if (!gKbText.empty()) gKbText.pop_back(); break;
    case KS_CLEAR:  gKbText.clear(); break;
    case KS_OK:     KbConfirm(); break;
    case KS_CANCEL: KbCancelEdit(); break;
    default: break;
    }
}
static void KbNav(MenuAction a)
{
    const int rows = 5;                                  // 4 character rows + the specials row
    if (a == ACT_UP)        gKbRow = (gKbRow + rows - 1) % rows;   // wrap
    else if (a == ACT_DOWN) gKbRow = (gKbRow + 1) % rows;         // wrap
    else if (a == ACT_LEFT || a == ACT_RIGHT)
    {
        int cols = (gKbRow < 4) ? (int)wcslen(kKbRows[gKbRow]) : (int)KS_COUNT;
        if (cols > 0) gKbCol = ((gKbCol + (a == ACT_RIGHT ? 1 : -1)) % cols + cols) % cols; // wrap
    }
    int cols = (gKbRow < 4) ? (int)wcslen(kKbRows[gKbRow]) : (int)KS_COUNT;
    if (gKbCol > cols - 1) gKbCol = cols - 1;            // clamp column after a row change
    if (gKbCol < 0) gKbCol = 0;
}

// --- file browser ---
static std::wstring PathJoin(const std::wstring& a, const std::wstring& b)
{
    if (a.empty()) return b;
    std::wstring r = a;
    if (r.back() != L'\\' && r.back() != L'/') r += L'\\';
    r += b;
    return r;
}
static std::wstring PathUp(const std::wstring& p)
{
    std::wstring t = p;
    while (!t.empty() && (t.back() == L'\\' || t.back() == L'/')) t.pop_back();
    size_t q = t.find_last_of(L"\\/");
    if (q == std::wstring::npos) return std::wstring();  // was a drive root -> drive list
    return t.substr(0, q + 1);
}
static void PopulateBrowse()
{
    gBrowseEntries.clear();
    gBrowseSel = 0;
    if (gBrowsePath.empty())                              // drive list
    {
        wchar_t drives[256] = {};
        GetLogicalDriveStringsW(255, drives);
        for (wchar_t* d = drives; *d; d += wcslen(d) + 1)
        { BrowseEntry e; e.name = d; e.isDir = true; gBrowseEntries.push_back(e); }
        return;
    }
    std::wstring spec = gBrowsePath;
    if (spec.back() != L'\\' && spec.back() != L'/') spec += L'\\';
    spec += L'*';
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(spec.c_str(), &fd);
    std::vector<BrowseEntry> dirs, files;
    if (h != INVALID_HANDLE_VALUE)
    {
        do {
            std::wstring nm = fd.cFileName;
            if (nm == L"." || nm == L"..") continue;
            BrowseEntry e; e.name = nm;
            e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (e.isDir)                       dirs.push_back(e);
            else if (!gBrowseImagesOnly || HasImageExt(nm)) files.push_back(e);   // icon picker: images only
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (auto& e : dirs)  gBrowseEntries.push_back(e);
    for (auto& e : files) gBrowseEntries.push_back(e);
}
// Open the file browser writing into 'field'. imagesOnly = icon picker (list only image files).
static void FilesStart(EditField field, bool imagesOnly)
{
    gEditField = field;
    gBrowseImagesOnly = imagesOnly;
    gFilesReturn = gUi;              // return to whatever opened the browser (edit popup, or Settings tab)
    // Browsing for an icon image opens the user's assets\icons folder by default (where they drop their
    // own images); everything else starts at the drive list.
    if ((field == EF_ITEM_ICON || field == EF_CAT_ICON) && !gUserIconDir.empty())
        gBrowsePath = gUserIconDir;
    else
        gBrowsePath.clear();
    PopulateBrowse();
    gUi = UI_FILES;
}
static void FilesNav(MenuAction a)
{
    int n = (int)gBrowseEntries.size();
    if (a == ACT_UP)   { if (n > 0) gBrowseSel = (gBrowseSel + n - 1) % n; }   // wrap
    else if (a == ACT_DOWN) { if (n > 0) gBrowseSel = (gBrowseSel + 1) % n; }  // wrap
    else if (a == ACT_OK)
    {
        if (gBrowseSel < 0 || gBrowseSel >= n) return;
        const BrowseEntry& e = gBrowseEntries[gBrowseSel];
        if (e.isDir) { gBrowsePath = PathJoin(gBrowsePath, e.name); PopulateBrowse(); }
        else {
            std::wstring full = PathJoin(gBrowsePath, e.name);
            EditField f = gEditField;
            ApplyEditField(full);
            std::wstring base = e.name;              // file name without extension, for auto Title
            size_t dot = base.find_last_of(L'.');
            if (dot != std::wstring::npos) base = base.substr(0, dot);
            if (f == EF_ITEM_EXE)                    // picking the program auto-fills Title + icon
            {
                MenuItem* it = CurItem();
                if (it) { it->title = base; it->icon = full; SaveConfig(); }  // icon = the file's own icon (LoadIcon extracts)
            }
            else if (f == EF_CAT_EXE)                // top-level item: same auto-fill onto the tab
            {
                MenuCategory* cc = CurCat();
                if (cc) { cc->title = base; cc->icon = full; SaveConfig(); }
            }
            gEditField = EF_NONE; gUi = gFilesReturn;
        }
    }
    else if (a == ACT_CANCEL)
    {
        if (gBrowsePath.empty()) { gEditField = EF_NONE; gUi = gFilesReturn; }
        else { gBrowsePath = PathUp(gBrowsePath); PopulateBrowse(); }
    }
}

// --- action-menu operations (all act on the currently selected, on-screen item/category) ---
static void EditAddItem()                       // append a blank item to the current category + select it
{
    MenuCategory* c = CurCat();
    if (!c) return;
    MenuItem ni; ni.type = L"app"; ni.title = L"New item";   // no default icon (bundled glyph set removed)
    c->items.push_back(std::move(ni));
    gItemSel[gCatSel] = (int)c->items.size() - 1;
    RetargetItems(true); SaveConfig();
}
static void EditAddCategory()                   // append a new category at the end of the row
{
    MenuCategory nc; nc.title = L"New category";   // no default icon (bundled glyph set removed)
    int at = (int)gMenu.size();
    gMenu.insert(gMenu.begin() + at, std::move(nc));
    gItemSel.insert(gItemSel.begin() + at, 0);
    gCatSel = at;
    gCatTween.set((float)gCatSel);
    RetargetItems(true); SaveConfig();
}
static void EditAddTopItem()                    // insert a TOP-LEVEL direct-launch item (a launcher tab)
{
    MenuCategory nc; nc.title = L"New item"; nc.isItem = true;   // no default icon (bundled glyph set removed)
    int at = (int)gMenu.size();
    gMenu.insert(gMenu.begin() + at, std::move(nc));
    gItemSel.insert(gItemSel.begin() + at, 0);
    gCatSel = at; gCatTween.set((float)gCatSel);
    gEditFocusCat = true;                        // a top-level item is focused as its tab (no sub-items)
    RetargetItems(true); SaveConfig();
}
static void EditMoveItem(int d)                 // reorder the selected item within its category
{
    MenuCategory* c = CurCat();
    if (!c) return;
    int s = gItemSel[gCatSel], ns = s + d;
    if (s < 0 || ns < 0 || ns >= (int)c->items.size()) return;
    std::swap(c->items[s], c->items[ns]);
    gItemSel[gCatSel] = ns;
    RetargetItems(true); SaveConfig();
}
static void EditDeleteItem()
{
    MenuCategory* c = CurCat();
    if (!c || c->items.empty()) return;
    c->items.erase(c->items.begin() + gItemSel[gCatSel]);
    if (gItemSel[gCatSel] >= (int)c->items.size()) gItemSel[gCatSel] = (int)c->items.size() - 1;
    if (gItemSel[gCatSel] < 0) gItemSel[gCatSel] = 0;
    RetargetItems(true); SaveConfig();
}
static void EditDeleteCategory()                // keep at least one category
{
    if ((int)gMenu.size() <= 1) return;
    gMenu.erase(gMenu.begin() + gCatSel);
    gItemSel.erase(gItemSel.begin() + gCatSel);
    int maxReal = (int)gMenu.size();
    if (gCatSel >= maxReal) gCatSel = maxReal - 1;
    if (gCatSel < 0) gCatSel = 0;
    gCatTween.set((float)gCatSel);
    RetargetItems(true); SaveConfig();
}

// Reorder the current category among the categories.
static void EditMoveCategory(int d)
{
    int realCount = (int)gMenu.size();
    int s = gCatSel, ns = s + d;
    if (ns < 0 || ns >= realCount) return;
    std::swap(gMenu[s], gMenu[ns]);
    std::swap(gItemSel[s], gItemSel[ns]);
    gCatSel = ns; gCatTween.retarget((float)gCatSel, XMB_DELAY);
    RetargetItems(true); SaveConfig();
}
// Move the current item into the adjacent real category (append there + follow it). No wrap.
static void EditMoveItemToCategory(int d)
{
    int realCount = (int)gMenu.size();
    if (realCount <= 1) return;
    MenuCategory* c = CurCat();
    if (!c || c->items.empty()) return;
    int nc = gCatSel + d;
    if (nc < 0 || nc >= realCount) return;               // don't move off the ends
    int s = gItemSel[gCatSel];
    MenuItem moved = std::move(c->items[s]);
    c->items.erase(c->items.begin() + s);
    if (gItemSel[gCatSel] >= (int)c->items.size()) gItemSel[gCatSel] = (int)c->items.size() - 1;
    if (gItemSel[gCatSel] < 0) gItemSel[gCatSel] = 0;
    gMenu[nc].items.push_back(std::move(moved));
    gCatSel = nc;
    gItemSel[gCatSel] = (int)gMenu[nc].items.size() - 1;
    gCatTween.retarget((float)gCatSel, XMB_DELAY);
    RetargetItems(true); SaveConfig();
}
// Edit-mode navigation. Directions transpose with orientation. Not moving: category axis switches
// tabs, item axis walks items (stepping up off item 0 focuses the category tab so it can be edited).
// In move mode the focused item/category is repositioned live (item: up/down within, left/right across
// categories; category: left/right reorders the tabs). OK opens the context popup; B leaves / cancels.
static void EditNav(MenuAction a)
{
    int realCount = (int)gMenu.size();
    int itemDir = 0, catDir = 0;
    switch (a) {
    case ACT_UP:    if (gVertical) catDir = -1; else itemDir = -1; break;
    case ACT_DOWN:  if (gVertical) catDir = +1; else itemDir = +1; break;
    case ACT_LEFT:  if (gVertical) itemDir = -1; else catDir = -1; break;
    case ACT_RIGHT: if (gVertical) itemDir = +1; else catDir = +1; break;
    case ACT_OK:
        if (gMoveMode) { gMoveMode = false; SaveConfig(); return; }
        if (gEditOnAddCat)                                   // "+" add slot -> choose category or item
        { gActPage = AP_ADDCHOICE; gActionSel = 0; gUi = UI_ACTION; return; }
        if (!gEditFocusCat)
        {
            int n = (gCatSel < (int)gMenu.size()) ? (int)gMenu[gCatSel].items.size() : 0;
            int s = (gCatSel < (int)gItemSel.size()) ? gItemSel[gCatSel] : 0;
            if (s >= n)                                      // "+" add-item slot -> add + edit it
            { EditAddItem(); gActPage = AP_ITEM_FIELDS; gActionSel = 0; gUi = UI_ACTION; return; }
        }
        OpenContextMenu();
        return;
    case ACT_CANCEL: if (gMoveMode) { gMoveMode = false; SaveConfig(); } else LeaveEditMode(); return;
    default: return;
    }
    // Part C2: reversed submenu flows the other way, so invert the item axis (the visual direction the user
    // presses then matches the movement). Only the direction actions reach here (OK / Cancel returned above).
    if (itemDir && CurTune().subReverse) itemDir = -itemDir;

    if (gMoveMode)
    {
        if (gEditFocusCat) { if (catDir) EditMoveCategory(catDir); }
        else if (itemDir)  EditMoveItem(itemDir);
        else if (catDir)   EditMoveItemToCategory(catDir);
        return;
    }

    // --- category axis ---
    if (catDir)
    {
        if (gEditOnAddCat)
        {
            if (catDir < 0)                                  // leave the "+" slot back to the last category
            { gEditOnAddCat = false; gEditFocusCat = true; gCatTween.retarget((float)gCatSel, XMB_DELAY); }
            return;                                          // catDir > 0: already rightmost, stay
        }
        if (catDir > 0 && gCatSel == realCount - 1)          // past the last category -> "+" add-category slot
        { gEditOnAddCat = true; gEditFocusCat = true; gCatTween.retarget((float)realCount, XMB_DELAY); return; }
        MoveCategory(catDir);
        MenuCategory* c = CurCat();
        if (!c || c->items.empty()) gEditFocusCat = true;    // empty category: focus the tab itself
        return;
    }

    // --- item axis ---
    if (itemDir)
    {
        if (gEditOnAddCat) return;                           // no item axis while on the add-category slot
        MenuCategory* c = CurCat();
        int n = c ? (int)c->items.size() : 0;
        if (gEditFocusCat)
        {
            if (c && c->isItem) return;                      // top-level item has no sub-menu to descend into
            if (itemDir > 0)                                 // descend into items (or the "+" if empty)
            { gEditFocusCat = false; gItemSel[gCatSel] = 0; RetargetItems(false); }
        }
        else
        {
            int s  = (gCatSel < (int)gItemSel.size()) ? gItemSel[gCatSel] : 0;
            int ns = s + itemDir;
            if (ns < 0)       gEditFocusCat = true;          // up off item 0 -> the category tab
            else if (ns > n)  { /* past the "+" slot: stay */ }
            else { gItemSel[gCatSel] = ns; RetargetItems(false); }   // ns == n is the "+" add slot
        }
    }
}
static void LeaveEditMode()
{
    gEditMode = false; gMoveMode = false; gEditFocusCat = false; gEditOnAddCat = false;
    for (size_t c = 0; c < gItemSel.size() && c < gMenu.size(); ++c)   // clamp any "+"-slot selection back
    { int mx = (int)gMenu[c].items.size() - 1; if (mx < 0) mx = 0; if (gItemSel[c] > mx) gItemSel[c] = mx; }
    RetargetItems(true);        // resize the item tweens back down (drop the phantom add slot) + snap
    // Edit mode is only ever entered from the Settings "Edit" section, so backing out of it (ESC / Backspace
    // / B) returns to the Settings popup (section list, Edit highlighted) rather than the plain menu.
    // (OpenSettings() also calls this, then sets its own start state, so it is unaffected.)
    gUi = UI_SETTINGS; gSettingsDepth = 0; gSectionSel = SEC_EDIT;
}

static void ActionActivate()
{
    MenuCategory* c = CurCat();
    MenuItem*     it = CurItem();
    if (gActPage == AP_ITEM_FIELDS)
    {
        switch (gActionSel)
        {
        case 0: if (it) { FilesStart(EF_ITEM_EXE, false); return; }                 break;  // Path: browse for file
        case 1: if (it) KbStart(EF_ITEM_TITLE, L"Item title",     it->title);       return;
        case 2: if (it) KbStart(EF_ITEM_ARGS,  L"Arguments",      it->arguments);   return;
        case 3: if (it) { FilesStart(EF_ITEM_ICON, true); return; }                 break;  // Icon: browse image
        case 4: if (it) { it->trackWindow = !it->trackWindow; SaveConfig(); }        return;  // toggle window-watch (stay on page)
        case 5: gActPage = AP_CTX; gActionSel = 0;                                   break;  // Back
        }
        return;
    }
    if (gActPage == AP_CAT_FIELDS)
    {
        switch (gActionSel)
        {
        case 0: if (c) KbStart(EF_CAT_TITLE, L"Category title", c->title); return;
        case 1: if (c) { FilesStart(EF_CAT_ICON, true); return; }           break;  // Icon: browse image
        case 2: gActPage = AP_CTX; gActionSel = 0;                          break;  // Back
        }
        return;
    }
    if (gActPage == AP_TOPITEM_FIELDS)                        // top-level item: path/title/icon on the tab
    {
        switch (gActionSel)
        {
        case 0: if (c) { FilesStart(EF_CAT_EXE, false); return; }           break;  // Path: browse for file
        case 1: if (c) KbStart(EF_CAT_TITLE, L"Item title", c->title);      return;
        case 2: if (c) { FilesStart(EF_CAT_ICON, true); return; }           break;  // Icon: browse image
        case 3: if (c) { c->trackWindow = !c->trackWindow; SaveConfig(); }  return;  // toggle window-watch (stay on page)
        case 4: gActPage = AP_CTX; gActionSel = 0;                          break;  // Back
        }
        return;
    }
    if (gActPage == AP_ADDCHOICE)                            // the top-menu "+" chooser
    {
        switch (gActionSel)
        {
        case 0: gEditOnAddCat = false; EditAddTopItem();  gActPage = AP_TOPITEM_FIELDS; gActionSel = 0; break; // Add item
        case 1: gEditOnAddCat = false; EditAddCategory(); gEditFocusCat = true; gActPage = AP_CAT_FIELDS; gActionSel = 0; break; // Add category
        case 2: gUi = UI_BROWSE;                                                                        break; // Close (stay on the "+" slot)
        }
        return;
    }
    // AP_CTX (top-level context menu) - the entries depend on whether an item or the category is focused.
    if (gEditFocusCat && FocusCatIsItem())                   // kTopItemCtx (a top-level launcher tab)
    {
        switch (gActionSel)
        {
        case 0: if (c) { gActPage = AP_TOPITEM_FIELDS; gActionSel = 0; } break;      // Edit item...
        case 1: gMoveMode = true; gUi = UI_BROWSE;                       break;      // Move (live)
        case 2: EditDeleteCategory(); gUi = UI_BROWSE;                   break;      // Delete item (the whole tab)
        case 3: gUi = UI_BROWSE;                                         break;      // Close
        }
        return;
    }
    if (gEditFocusCat)                                        // kCatCtx (a container category)
    {
        switch (gActionSel)
        {
        case 0: if (c) { gActPage = AP_CAT_FIELDS; gActionSel = 0; } break;          // Edit category...
        case 1: gMoveMode = true; gUi = UI_BROWSE;                   break;          // Move (live)
        case 2: EditDeleteCategory(); gUi = UI_BROWSE;               break;          // Delete category
        case 3: EditAddTopItem();  gActPage = AP_TOPITEM_FIELDS; gActionSel = 0; break; // Add item (top level)
        case 4: EditAddCategory(); gEditFocusCat = true;  gActPage = AP_CAT_FIELDS;  gActionSel = 0; break; // Add category
        case 5: gUi = UI_BROWSE;                                     break;          // Close
        }
        return;
    }
    switch (gActionSel)                                       // kItemCtx {Edit, Move, Delete, Close}
    {
    case 0: if (it) { gActPage = AP_ITEM_FIELDS; gActionSel = 0; } break;            // Edit item...
    case 1: if (it) { gMoveMode = true; gUi = UI_BROWSE; }         break;            // Move (live)
    case 2: EditDeleteItem();  gUi = UI_BROWSE;                    break;            // Delete item
    case 3: gUi = UI_BROWSE;   break;                                               // Close
    }
}
static void ActionNav(MenuAction a)
{
    int n = ActCount();
    if      (a == ACT_UP)     gActionSel = (gActionSel + n - 1) % n;
    else if (a == ACT_DOWN)   gActionSel = (gActionSel + 1) % n;
    else if (a == ACT_OK)     ActionActivate();
    else if (a == ACT_CANCEL) { if (gActPage == AP_CTX) gUi = UI_BROWSE;   // back to edit-browse
                                else { gActPage = AP_CTX; gActionSel = 0; } }  // field page -> context
}
static void OpenContextMenu() { gActPage = AP_CTX; gActionSel = 0; gUi = UI_ACTION; }

// Drop straight into live edit mode (on a real category). Reached from the Settings "Edit" section,
// which no longer opens a submenu - selecting Edit enters edit mode immediately.
static void EnterEditMode()
{
    gEditMode = true; gMoveMode = false; gEditOnAddCat = false; gUi = UI_BROWSE; gSettingsDepth = 0;
    // Start on the first item, or on the category tab if the category is empty.
    gEditFocusCat = (gCatSel < (int)gMenu.size() && gMenu[gCatSel].items.empty());
    // Entering edit mode adds the trailing "+" (Add item) slot to ActiveCount, so re-size/re-snap the item
    // tweens now. Without this the item loop (bounded by gItemYT.size()) skips the "+" until the next
    // RetargetItems - which is why on a fresh, empty category the "+" only appeared after moving down.
    RetargetItems(true);
}

// Enter/leave edit mode (from browse). Also used to back out of any sub-mode.