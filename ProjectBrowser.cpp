// ProjectBrowser: Radiance's Project dock and code editor as plain Radiant dock panes (no Radiance needed).
//
// Project: the active map's usermaps folder as a folder tree, the selected
// folder's items as tiles or a list, search and a file-type filter, creating folders and files, and (additions)
// renaming, deleting to the Recycle Bin and showing items in Explorer. Double-clicking a folder opens it, a text file
// (GSC, CSC, zone, CSV, GDT, JSON...) opens in the Code Editor pane.
#define MODTOOLS_PLUGIN_IMPL
#include "modtools_plugin.hpp"
#include "QtLite.h"
#include "ProjectFs.h"
#include "CodeEditor.h"
#include "RadiantDock.h"
#include "Version.h"
#include <set>
#include <shellapi.h>

namespace
{
    void LogSink(const char* text) { if (rp::Bound()) rp::host->Log(rp::self, "%s", text); }
    const ModToolsUiApi* Ui() { return rp::host->ui; }
    using pfs::Utf8;
    using pfs::Wide;

    // ---- state --------------------------------------------------------------------------------------------------
    std::wstring g_mapPath, g_mapName, g_userRoot;
    std::vector<pfs::Folder> g_roots;
    std::vector<pfs::Folder*> g_folders;               // tree item id - 1 -> folder
    std::vector<ModToolsTreeItem> g_folderItems;
    std::wstring g_folder;                             // the selected folder's full path ("" = none)
    std::vector<pfs::Item> g_all, g_shown;            // the folder's items, and those passing search + type filter
    std::wstring g_selected;                           // the selected item's full path
    std::wstring g_search, g_type;
    bool g_tiles = true;
    std::set<std::wstring> g_openFolders;              // expanded folders, by full path (lower case)
    std::string g_listing;                             // signature of the folder's items, for refreshing
    ULONGLONG g_nextRefresh = 0;
    bool g_activatePending = false;
    int g_suppress = 0;
    struct Suppress { Suppress() { ++g_suppress; } ~Suppress() { --g_suppress; } };

    // ---- widgets ------------------------------------------------------------------------------------------------
    ModToolsPane g_pane = nullptr;
    ModToolsWidget g_root = nullptr, g_tree = nullptr, g_list = nullptr, g_rows = nullptr, g_searchEdit = nullptr;
    ModToolsWidget g_header = nullptr, g_count = nullptr, g_footer = nullptr, g_filterButton = nullptr, g_tilesButton = nullptr, g_listButton = nullptr;
    void* g_filter = nullptr;
    ql::Icon g_glyphFolderPlus, g_glyphFile, g_glyphFilter, g_glyphTiles, g_glyphList;
    ql::Icon g_small[5], g_large[5];                   // per pfs::Item::icon: file, folder, script, image, audio

    std::wstring Lower(std::wstring s) { for (wchar_t& c : s) c = wchar_t(towlower(c)); return s; }
    std::string Html(const std::wstring& w)
    {
        std::string s = Utf8(w), out;
        for (char c : s) { if (c == '<') out += "&lt;"; else if (c == '>') out += "&gt;"; else if (c == '&') out += "&amp;"; else out += c; }
        return ql::HtmlAscii(out);
    }

    std::vector<std::wstring> Roots()
    {
        std::vector<std::wstring> r;
        if (!g_userRoot.empty()) r.push_back(g_userRoot);
        return r;
    }
    pfs::Folder* FindFolder(const std::wstring& path)
    {
        for (pfs::Folder* f : g_folders) if (pfs::EqualsCi(f->fullPath, path)) return f;
        return nullptr;
    }
    std::wstring FolderDisplay()
    {
        if (pfs::Folder* f = FindFolder(g_folder)) return f->display;
        if (!g_folder.empty()) return g_folder;
        return g_mapPath.empty() ? L"No active Radiant map" : L"usermaps/" + g_mapName;
    }
    std::wstring Slashes(std::wstring s)
    {
        for (size_t at; (at = s.find(L" › ")) != std::wstring::npos;) s.replace(at, 3, L"/");
        return s;
    }

    // ---- header, footer -------------------------------------------------------------------------------------------
    void UpdateLabels()
    {
        std::string header = "<b>" + Html(FolderDisplay()) + "</b>";
        if (!g_type.empty()) header += " <span style='color:#808080'>&middot; " + Html(g_type) + " only</span>";
        Ui()->SetLabelText(g_header, header.c_str());
        std::string count;
        if (g_mapPath.empty() && g_folder.empty()) count = "No active Radiant map";
        else
        {
            std::string where = g_mapPath.empty() ? Utf8(FolderDisplay()) : "usermaps/" + Utf8(g_mapName);
            bool searching = g_search.find_first_not_of(L" \t") != std::wstring::npos;
            count = searching ? std::to_string(g_shown.size()) + " of " + std::to_string(g_all.size()) + " items   |   " + where
                              : std::to_string(g_all.size()) + " items   |   " + where;
        }
        Ui()->SetLabelText(g_count, ("<span style='color:#9a9a9a'>" + count + "</span>").c_str());
        std::wstring footer = !g_selected.empty() ? g_selected : Slashes(FolderDisplay());
        Ui()->SetLabelText(g_footer, ("<span style='color:#9a9a9a'>" + Html(footer) + "</span>").c_str());
        Ui()->SetToolTip(g_filterButton, g_type.empty() ? "Show one file type" : ("Showing " + Utf8(g_type) + " files only").c_str());
    }

    // ---- items --------------------------------------------------------------------------------------------------------
    std::string Signature(const std::vector<pfs::Item>& items)
    {
        std::string s;
        for (const pfs::Item& it : items) s += Utf8(it.name) + (it.folder ? "/" : ":" + std::to_string(it.size)) + "\n";
        return s;
    }

    void ShowItems()
    {
        g_shown = pfs::Filter(g_all, g_search, g_type);
        Suppress s;
        ql::SetUpdates(g_list, false); ql::SetUpdates(g_rows, false);
        Ui()->ListClear(g_list);
        Ui()->TreeClear(g_rows);
        int selectedRow = -1;
        for (size_t i = 0; i < g_shown.size(); ++i)
        {
            const pfs::Item& it = g_shown[i];
            Ui()->ListAdd(g_list, Utf8(it.name).c_str());
            ql::ListItemText(g_list, int(i), Utf8(it.name));
            Ui()->ListSetItemToolTip(g_list, int(i), Utf8(it.fullPath).c_str());
            ql::ListItemIcon(g_list, int(i), g_large[it.icon]);
            ModToolsTreeItem row = Ui()->TreeAdd(g_rows, nullptr, (Utf8(it.name) + "\t" + Utf8(it.type)).c_str());
            ql::TreeItemText(row, 0, Utf8(it.name));
            Ui()->TreeSetItemId(row, int64_t(i) + 1);
            ql::TreeItemIcon(row, 0, g_small[it.icon]);
            ql::TreeItemToolTip(row, 0, Utf8(it.fullPath));
            ql::TreeItemColor(row, 1, 0x8A8A8A);
            if (pfs::EqualsCi(it.fullPath, g_selected)) selectedRow = int(i);
        }
        if (selectedRow >= 0)
        {
            Ui()->ListSetCurrentRow(g_list, selectedRow);
            Ui()->TreeSetCurrent(g_rows, Ui()->TreeChild(g_rows, nullptr, selectedRow));
        }
        else g_selected.clear();
        ql::SetUpdates(g_list, true); ql::SetUpdates(g_rows, true);
        UpdateLabels();
    }

    void LoadItems()
    {
        std::wstring error;
        g_all = pfs::GetItems(g_folder, error);
        if (!error.empty()) rp::Log("could not read %s: %s", Utf8(g_folder).c_str(), Utf8(error).c_str());
        g_listing = Signature(g_all);
        ShowItems();
    }

    // ---- folder tree ------------------------------------------------------------------------------------------------
    void CaptureOpenFolders()
    {
        for (size_t i = 0; i < g_folderItems.size(); ++i)
            if (g_folderItems[i] && i < g_folders.size())
            {
                std::wstring key = Lower(g_folders[i]->fullPath);
                if (ql::TreeItemExpanded(g_folderItems[i])) g_openFolders.insert(key); else g_openFolders.erase(key);
            }
    }

    void AddFolder(pfs::Folder& f, ModToolsTreeItem parent)
    {
        ModToolsTreeItem item = Ui()->TreeAdd(g_tree, parent, Utf8(f.name).c_str());
        ql::TreeItemText(item, 0, Utf8(f.name));
        g_folders.push_back(&f);
        g_folderItems.push_back(item);
        Ui()->TreeSetItemId(item, int64_t(g_folders.size()));
        ql::TreeItemIcon(item, 0, g_small[1]);
        ql::TreeItemToolTip(item, 0, Utf8(f.fullPath));
        for (pfs::Folder& c : f.children) AddFolder(c, item);
        bool open = f.expanded || g_openFolders.count(Lower(f.fullPath)) > 0;
        Ui()->TreeSetExpanded(item, open ? 1 : 0);
    }

    void SelectFolderItem()
    {
        Suppress s;
        for (size_t i = 0; i < g_folders.size(); ++i)
            if (pfs::EqualsCi(g_folders[i]->fullPath, g_folder)) { Ui()->TreeSetCurrent(g_tree, g_folderItems[i]); ql::TreeScrollTo(g_tree, g_folderItems[i]); return; }
        Ui()->TreeSetCurrent(g_tree, nullptr);
    }

    void BuildTree()
    {
        CaptureOpenFolders();
        g_roots.clear();
        pfs::Folder user = pfs::BuildUserMapFolders(g_mapPath);
        if (!user.name.empty()) g_roots.push_back(std::move(user));   // only the open map's usermaps folder, like Radiance
        Suppress s;
        Ui()->TreeClear(g_tree);
        g_folders.clear();
        g_folderItems.clear();
        for (pfs::Folder& r : g_roots) AddFolder(r, nullptr);
        SelectFolderItem();
    }

    void SelectFolder(const std::wstring& path)
    {
        g_folder = path;
        g_selected.clear();
        SelectFolderItem();
        LoadItems();
    }

    // NavigateToProjectFolder: a folder from the items (it may be deeper than the tree's five levels).
    void Navigate(const std::wstring& path)
    {
        if (!FindFolder(path) && pfs::IsDirectory(path))
        {
            // open every level down to it
            for (std::wstring at = pfs::Parent(path); !at.empty() && at.size() > 3; at = pfs::Parent(at)) g_openFolders.insert(Lower(at));
            BuildTree();
        }
        SelectFolder(path);
    }

    void LoadMap()
    {
        std::wstring path = rp::Has(&rp::host->CurrentMapPath) && rp::host->CurrentMapPath ? rp::CurrentMapPath() : std::wstring();
        if (path == g_mapPath && !g_roots.empty()) return;
        g_mapPath = path;
        std::wstring root, name;
        if (pfs::UserMapRoot(path, root, name)) { g_userRoot = root; g_mapName = name; }
        else
        {
            g_userRoot.clear(); g_mapName.clear();
            if (!path.empty()) rp::Log("could not resolve the active map's usermaps project folder (%s)", Utf8(path).c_str());
        }
        BuildTree();
        // the map's own folder, like Radiance
        if (!g_userRoot.empty()) SelectFolder(g_userRoot);
        else if (!g_roots.empty()) SelectFolder(g_roots.front().fullPath);
        else SelectFolder(std::wstring());
    }

    // ---- actions ------------------------------------------------------------------------------------------------------
    const pfs::Item* Selected()
    {
        for (const pfs::Item& it : g_shown) if (pfs::EqualsCi(it.fullPath, g_selected)) return &it;
        return nullptr;
    }

    void Activate(const pfs::Item* it)
    {
        if (!it) return;
        if (it->folder) Navigate(it->fullPath);
        else if (pfs::IsEditableText(it->fullPath)) editor::Open(it->fullPath);   // scripts, CSVs, zones and other text
        else rp::Log("%s is not a text file the editor can open", Utf8(it->fullPath).c_str());
    }

    void CreateItem(bool folder)
    {
        if (g_folder.empty()) { ql::Warn(g_root, folder ? "Create Folder" : "Create File", "Open a map in Radiant first: the project is the map's usermaps folder."); return; }
        std::string label = "Created in " + Utf8(Slashes(FolderDisplay())) + "\n\n" + (folder ? "Folder name:" : "File name, including extension (optional):");
        std::string name;
        if (!ql::AskText(g_root, folder ? "Create Folder" : "Create File", label, "", name) || name.find_first_not_of(" \t") == std::string::npos) return;
        std::wstring error;
        std::wstring made = folder ? pfs::NewFolder(Roots(), g_folder, Wide(name), error) : pfs::NewFile(Roots(), g_folder, Wide(name), error);
        if (made.empty()) { ql::Warn(g_root, folder ? "Create Folder" : "Create File", Utf8(error)); return; }
        rp::Log("%s %s", folder ? "created project folder" : "created project file", Utf8(made).c_str());
        if (folder) { g_openFolders.insert(Lower(g_folder)); BuildTree(); SelectFolderItem(); }
        g_selected = made;
        LoadItems();
    }

    bool IsRoot(const std::wstring& path)
    {
        for (const pfs::Folder& r : g_roots)
        {
            if (pfs::EqualsCi(r.fullPath, path)) return true;
            if (pfs::EqualsCi(r.name, L"usermaps") && !r.children.empty() && pfs::EqualsCi(r.children[0].fullPath, path)) return true;   // the map's own folder
        }
        return false;
    }

    void RenameItem(const std::wstring& path)
    {
        if (path.empty() || IsRoot(path)) return;
        std::string name;
        if (!ql::AskText(g_root, "Rename", "New name:", Utf8(pfs::FileName(path)), name)) return;
        std::wstring error;
        std::wstring renamed = pfs::Rename(Roots(), path, Wide(name), error);
        if (renamed.empty()) { ql::Warn(g_root, "Rename", Utf8(error)); return; }
        if (renamed == pfs::FullPath(path)) return;
        editor::PathRenamed(path, renamed);
        bool wasDir = pfs::IsDirectory(renamed);
        if (pfs::EqualsCi(g_folder, path)) g_folder = renamed;
        else if (g_folder.size() > path.size() && pfs::EqualsCi(g_folder.substr(0, path.size()), path)) g_folder = renamed + g_folder.substr(path.size());
        if (wasDir) BuildTree();
        g_selected = renamed;
        LoadItems();
        SelectFolderItem();
    }

    void DeleteItem(const std::wstring& path)
    {
        if (path.empty() || IsRoot(path)) return;
        bool dir = pfs::IsDirectory(path);
        std::string what = Utf8(pfs::FileName(path));
        if (ql::Question(g_root, "Delete", "Move " + std::string(dir ? "the folder '" : "'") + what + "'" + (dir ? " and everything in it" : "") + " to the Recycle Bin?",
                         ql::BtnYes | ql::BtnNo, ql::BtnNo) != ql::BtnYes) return;
        std::wstring error;
        if (!pfs::DeleteToRecycleBin(Roots(), path, error)) { ql::Warn(g_root, "Delete", Utf8(error)); return; }
        rp::Log("moved %s to the Recycle Bin", Utf8(path).c_str());
        if (dir)
        {
            if (pfs::EqualsCi(g_folder, path) || (g_folder.size() > path.size() && pfs::EqualsCi(g_folder.substr(0, path.size() + 1), path + L"\\"))) g_folder = pfs::Parent(path);
            BuildTree();
            SelectFolderItem();
        }
        g_selected.clear();
        LoadItems();
    }

    void ShowInExplorer(const std::wstring& path)
    {
        if (path.empty()) return;
        std::wstring args = pfs::IsDirectory(path) && !pfs::EqualsCi(path, g_selected) ? L"\"" + path + L"\"" : L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }

    void CopyPath(const std::wstring& path)
    {
        if (path.empty() || !OpenClipboard(nullptr)) return;
        EmptyClipboard();
        size_t bytes = (path.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            memcpy(GlobalLock(mem), path.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        }
        CloseClipboard();
    }

    void ItemMenu(bool onItem)
    {
        ql::Menu m(g_root);
        if (!m.Ok()) return;
        const pfs::Item* it = onItem ? Selected() : nullptr;
        std::wstring path = it ? it->fullPath : g_folder;
        int open = -2, rename = -2, del = -2;
        if (it)
        {
            open = m.Add(it->folder ? "Open" : "Open in Code Editor", it->folder || pfs::IsEditableText(it->fullPath));
            rename = m.Add("Rename...\tF2");
            del = m.Add("Delete\tDel");
            m.Separator();
        }
        int newFolder = m.Add("New Folder...", !g_folder.empty());
        int newFile = m.Add("New File...", !g_folder.empty());
        m.Separator();
        int explorer = m.Add("Show in Explorer", !path.empty());
        int copy = m.Add("Copy Path", !path.empty());
        int refresh = m.Add("Refresh");
        int chosen = m.Exec();
        if (chosen < 0) return;
        std::wstring keep = it ? it->fullPath : std::wstring();
        if (chosen == open) { pfs::Item copyItem = *it; Activate(&copyItem); }
        else if (chosen == rename) RenameItem(keep);
        else if (chosen == del) DeleteItem(keep);
        else if (chosen == newFolder) CreateItem(true);
        else if (chosen == newFile) CreateItem(false);
        else if (chosen == explorer) ShowInExplorer(path);
        else if (chosen == copy) CopyPath(path);
        else if (chosen == refresh) { BuildTree(); LoadItems(); }
    }

    void FolderMenu(int folder)
    {
        if (folder < 0 || folder >= int(g_folders.size())) return;
        std::wstring path = g_folders[size_t(folder)]->fullPath;
        bool root = IsRoot(path);
        ql::Menu m(g_root);
        if (!m.Ok()) return;
        int newFolder = m.Add("New Folder...");
        int newFile = m.Add("New File...");
        m.Separator();
        int rename = m.Add("Rename...\tF2", !root);
        int del = m.Add("Delete\tDel", !root);
        m.Separator();
        int explorer = m.Add("Show in Explorer");
        int copy = m.Add("Copy Path");
        int refresh = m.Add("Refresh");
        int chosen = m.Exec();
        if (chosen < 0) return;
        if (chosen == newFolder) CreateItem(true);
        else if (chosen == newFile) CreateItem(false);
        else if (chosen == rename) RenameItem(path);
        else if (chosen == del) DeleteItem(path);
        else if (chosen == explorer) ShowInExplorer(path);
        else if (chosen == copy) CopyPath(path);
        else if (chosen == refresh) { BuildTree(); LoadItems(); }
    }

    void FilterMenu()
    {
        ql::Menu m(g_filterButton);
        if (!m.Ok()) return;
        std::vector<std::wstring> labels = pfs::TypeLabels(g_all);
        int all = m.Add("All types", true, g_type.empty() ? 1 : 0);
        m.Separator();
        std::vector<int> ids;
        for (const std::wstring& l : labels) ids.push_back(m.Add(Utf8(l), true, pfs::EqualsCi(l, g_type) ? 1 : 0));
        int chosen = m.Exec();
        if (chosen < 0) return;
        if (chosen == all) g_type.clear();
        for (size_t i = 0; i < ids.size(); ++i) if (chosen == ids[i]) g_type = labels[i];
        ShowItems();
    }

    void SetMode(bool tiles)
    {
        g_tiles = tiles;
        Ui()->SetVisible(g_list, tiles);
        Ui()->SetVisible(g_rows, !tiles);
        rp::ConfigSet("project", "tiles", tiles ? "1" : "0");
    }

    // ---- events ---------------------------------------------------------------------------------------------------------
    int RowOfCurrent()
    {
        if (g_tiles) return Ui()->ListCurrentRow(g_list);
        ModToolsTreeItem cur = Ui()->TreeCurrent(g_rows);
        return cur ? int(Ui()->TreeItemId(cur)) - 1 : -1;
    }
    void SyncSelected()
    {
        int row = RowOfCurrent();
        g_selected = row >= 0 && row < int(g_shown.size()) ? g_shown[size_t(row)].fullPath : std::wstring();
        UpdateLabels();
    }

    bool ViewEvent(void*, void* watched, int type, void* ev)
    {
        using namespace ql;
        void* listVp = Viewport(g_list), *rowsVp = Viewport(g_rows), *treeVp = Viewport(g_tree);
        bool items = watched == g_list || watched == g_rows, itemsVp = watched == listVp || watched == rowsVp;
        if (type == EvShortcutOverride && (items || watched == g_tree))
        {
            int key = KeyCode(ev), mods = KeyMods(ev) & (CtrlMod | AltMod | ShiftMod);
            if ((key == Key_Delete || key == Key_F2 || key == Key_Return || key == Key_Enter) && mods == 0) AcceptEvent(ev);
            return false;
        }
        if (type == EvKeyPress && (items || watched == g_tree))
        {
            int key = KeyCode(ev), mods = KeyMods(ev) & (CtrlMod | AltMod | ShiftMod);
            if (mods != 0) return false;
            std::wstring target = items ? g_selected : g_folder;
            if (key == Key_Delete) { DeleteItem(target); return true; }
            if (key == Key_F2) { RenameItem(target); return true; }
            if ((key == Key_Return || key == Key_Enter) && items) { g_activatePending = true; return true; }
            return false;
        }
        if (type == EvMouseButtonDblClick && itemsVp)
        {
            bool hit = watched == listVp ? ListItemUnderCursor(g_list) : TreeItemUnderCursor(g_rows) != nullptr;
            if (hit) g_activatePending = true;   // after Qt has made the row current
            return false;
        }
        if (type == EvContextMenu && itemsVp)
        {
            bool hit = watched == listVp ? ListItemUnderCursor(g_list) : TreeItemUnderCursor(g_rows) != nullptr;
            if (hit) SyncSelected();
            ItemMenu(hit && !g_selected.empty());
            return true;
        }
        if (type == EvContextMenu && watched == treeVp)
        {
            ModToolsTreeItem item = TreeItemUnderCursor(g_tree);
            if (!item) return true;
            int folder = int(Ui()->TreeItemId(item)) - 1;
            if (folder >= 0 && folder < int(g_folders.size()) && !pfs::EqualsCi(g_folders[size_t(folder)]->fullPath, g_folder)) SelectFolder(g_folders[size_t(folder)]->fullPath);
            FolderMenu(folder);
            return true;
        }
        return false;
    }

    void OnFolderChanged(void*, int what)
    {
        if (g_suppress > 0 || what != 0) return;
        ModToolsTreeItem cur = Ui()->TreeCurrent(g_tree);
        int index = cur ? int(Ui()->TreeItemId(cur)) - 1 : -1;
        if (index < 0 || index >= int(g_folders.size())) return;
        if (pfs::EqualsCi(g_folders[size_t(index)]->fullPath, g_folder)) return;
        g_folder = g_folders[size_t(index)]->fullPath;
        g_selected.clear();
        LoadItems();
    }
    void OnListRow(void*, int) { if (g_suppress == 0 && g_tiles) SyncSelected(); }
    void OnRowsChanged(void*, int what) { if (g_suppress == 0 && what == 0 && !g_tiles) SyncSelected(); }
    void OnNewFolder(void*) { CreateItem(true); }
    void OnNewFile(void*) { CreateItem(false); }
    void OnFilterClicked(void*) { FilterMenu(); }
    void OnTiles(void*) { SetMode(true); }
    void OnList(void*) { SetMode(false); }

    // ---- pane -----------------------------------------------------------------------------------------------------------
    ModToolsWidget ToolButton(const char* fallback, ql::Icon& icon, ql::Glyph glyph, const char* tip, ModToolsCallback fn, ModToolsWidget box)
    {
        ModToolsWidget b = Ui()->PushButton("", g_root);
        if (ql::MakeGlyph(icon, glyph) && ql::SetButtonIcon(b, icon)) { if (ql::A().QWidget_setFixedWidth) ql::A().QWidget_setFixedWidth(b, 26); }
        else Ui()->SetButtonText(b, fallback);
        Ui()->SetToolTip(b, tip);
        rp::host->OnClicked(rp::self, b, fn, nullptr);
        Ui()->BoxAddWidget(box, b, 0);
        return b;
    }

    void MakeIcons()
    {
        const ql::Glyph glyphs[5] = { ql::Glyph::File, ql::Glyph::Folder, ql::Glyph::Script, ql::Glyph::Image, ql::Glyph::Audio };
        for (int i = 0; i < 5; ++i)
        {
            uint32_t tint = i == 1 ? 0xD6B05C : 0x9AA4B0;   // Radiance: folders gold, files grey-blue
            ql::MakeGlyph(g_small[i], glyphs[i], tint, 16);
            ql::MakeGlyph(g_large[i], glyphs[i], tint, 48);
        }
    }

    bool BuildPane()
    {
        const ModToolsUiApi* ui = Ui();
        if (!ui || !rp::HasUi(&ui->Splitter) || !rp::Has(&rp::host->OnChanged) || !rp::host->OnChanged) { rp::Log("this MTPlugins host lacks the panel widget API; update MTPlugins"); return false; }
        MakeIcons();
        g_root = ui->Widget(nullptr);
        ModToolsWidget v = ui->VBox(g_root);
        ui->LayoutMargins(v, 4, 4, 4, 2);
        ui->LayoutSpacing(v, 3);

        // toolbar: create folder, create file, search, filter, tiles / list
        ModToolsWidget bar = ui->HBox();
        ui->LayoutSpacing(bar, 3);
        ToolButton("+ Folder", g_glyphFolderPlus, ql::Glyph::FolderPlus, "Create a folder in the selected usermap directory", OnNewFolder, bar);
        ToolButton("+ File", g_glyphFile, ql::Glyph::File, "Create an empty file in the selected usermap directory", OnNewFile, bar);
        g_searchEdit = ui->LineEdit("", g_root);
        ui->SetPlaceholder(g_searchEdit, "Search project files...");
        ui->SetMaximumWidth(g_searchEdit, 260);
        ui->BoxAddWidget(bar, g_searchEdit, 1);
        ui->BoxAddStretch(bar, 1);
        g_filterButton = ToolButton("Filter", g_glyphFilter, ql::Glyph::Filter, "Show one file type", OnFilterClicked, bar);
        g_tilesButton = ToolButton("Tiles", g_glyphTiles, ql::Glyph::Tiles, "Tiles", OnTiles, bar);
        g_listButton = ToolButton("List", g_glyphList, ql::Glyph::List, "List", OnList, bar);
        ui->BoxAddLayout(v, bar, 0);

        // folder tree | header + items
        ModToolsWidget split = ui->Splitter(1, g_root);
        g_tree = ui->TreeWidget(1, split);
        if (ql::A().QTreeView_setIndentation) ql::A().QTreeView_setIndentation(g_tree, 14);
        ui->SplitterAdd(split, g_tree, 0);
        ModToolsWidget right = ui->Widget(split);
        ModToolsWidget rv = ui->VBox(right);
        ui->LayoutMargins(rv, 0, 0, 0, 0);
        ui->LayoutSpacing(rv, 2);
        ModToolsWidget head = ui->HBox();
        ui->LayoutMargins(head, 6, 2, 6, 2);
        g_header = ui->Label("", right);
        ql::IgnoreWidth(g_header);
        ui->BoxAddWidget(head, g_header, 1);
        g_count = ui->Label("", right);
        ui->BoxAddWidget(head, g_count, 0);
        ui->BoxAddLayout(rv, head, 0);
        g_list = ui->ListWidget(right);
        ql::ListViewMode(g_list, true, 48, 84, 84);
        ui->BoxAddWidget(rv, g_list, 1);
        g_rows = ui->TreeWidget(2, right);
        if (ql::A().QTreeView_setRootIsDecorated) ql::A().QTreeView_setRootIsDecorated(g_rows, false);
        if (ql::A().QTreeView_header)
            if (void* header = ql::A().QTreeView_header(g_rows))
            {
                if (ql::A().QHeaderView_setStretchLastSection) ql::A().QHeaderView_setStretchLastSection(header, false);
                if (ql::A().QHeaderView_setSectionResizeMode) { ql::A().QHeaderView_setSectionResizeMode(header, 0, 1); ql::A().QHeaderView_setSectionResizeMode(header, 1, 3); }
            }
        ui->BoxAddWidget(rv, g_rows, 1);
        ui->SplitterAdd(split, right, 1);
        ui->BoxAddWidget(v, split, 1);

        g_footer = ui->Label("", g_root);
        ql::IgnoreWidth(g_footer);
        ui->BoxAddWidget(v, g_footer, 0);

        rp::host->OnChanged(rp::self, g_tree, OnFolderChanged, nullptr);
        rp::host->OnChanged(rp::self, g_list, OnListRow, nullptr);
        rp::host->OnChanged(rp::self, g_rows, OnRowsChanged, nullptr);
        g_filter = ql::MakeFilter(ViewEvent, nullptr);
        if (g_filter)
            for (void* w : { g_list, g_rows, g_tree }) { ql::Install(w, g_filter); ql::Install(ql::Viewport(w), g_filter); }
        else rp::Log("Qt event filter unavailable: no double-click, context menu or keys in the Project pane");

        SetMode(rp::ConfigGetBool("project", "tiles", true));
        g_pane = rp::host->CreatePane(rp::self, "Project", g_root, 760, 300);
        if (!g_pane) return false;
        if (!rp::ConfigGetBool("pane", "placed", false) && rp::Has(&rp::host->TabPaneWith) && rp::host->TabPaneWith)
        {
            int r = rp::host->TabPaneWith(rp::self, g_pane, "Console");
            rp::ConfigSet("pane", "placed", "1");
            rp::Log("first placement beside the Console: %d", r);
        }
        if (ModToolsWidget menu = rp::host->AddMenu(rp::self, "Project"))
        {
            rp::host->AddMenuAction(rp::self, menu, rp::host->PaneToggleAction(rp::self, g_pane));
            rdock::AddEdgeItems(menu, g_root, "Project");
        }
        rp::host->ShowPane(rp::self, g_pane, 1);
        return true;
    }

    // ---- lifecycle ------------------------------------------------------------------------------------------------------
    void OnMapLoaded(const wchar_t*) { g_mapPath.clear(); LoadMap(); }

    void OnTick()
    {
        if (!g_pane) return;
        editor::Tick();
        if (g_activatePending) { g_activatePending = false; SyncSelected(); if (const pfs::Item* it = Selected()) { pfs::Item copy = *it; Activate(&copy); } }
        if (rp::Has(&rp::host->PaneVisible) && rp::host->PaneVisible && !rp::host->PaneVisible(rp::self, g_pane)) return;
        std::wstring search = Wide(rp::EditText(g_searchEdit));
        if (search != g_search) { g_search = search; ShowItems(); }
        ULONGLONG now = GetTickCount64();
        if (now >= g_nextRefresh)
        {
            // follow changes made outside (Explorer, the compiler, other tools)
            g_nextRefresh = now + 2000;
            if (!g_folder.empty())
            {
                std::wstring error;
                auto items = pfs::GetItems(g_folder, error);
                std::string sig = Signature(items);
                if (sig != g_listing)
                {
                    size_t dirsBefore = std::count_if(g_all.begin(), g_all.end(), [](const pfs::Item& i) { return i.folder; });
                    size_t dirsAfter = std::count_if(items.begin(), items.end(), [](const pfs::Item& i) { return i.folder; });
                    g_all = items; g_listing = sig;
                    if (!pfs::IsDirectory(g_folder)) { g_folder = pfs::Parent(g_folder); BuildTree(); LoadItems(); }
                    else { if (dirsBefore != dirsAfter) BuildTree(); ShowItems(); }
                }
            }
        }
    }

    int OnLoad()
    {
        if (!rp::InRadiant()) return 0;
        ql::LogSink() = LogSink;
        if (!ql::Bind()) rp::Log("Qt binding incomplete: icons, menus, keys and colouring are unavailable");
        if (!BuildPane()) return 0;
        if (!editor::Create()) rp::Log("the Code Editor pane could not be created");
        LoadMap();
        rp::Log("ProjectBrowser %s loaded", PROJECTBROWSER_VERSION);
        return 1;
    }

    void OnUnload()
    {
        // a disable from the Plugins window: offer to save edited files first (Radiant's exit is handled by the editor)
        if (editor::AnyModified()) editor::ConfirmCloseAll(g_root);
        editor::Destroy();
        ql::Shutdown();
        for (ql::Icon* i : { &g_glyphFolderPlus, &g_glyphFile, &g_glyphFilter, &g_glyphTiles, &g_glyphList }) i->Reset();
        for (int i = 0; i < 5; ++i) { g_small[i].Reset(); g_large[i].Reset(); }
        g_pane = nullptr;
    }

    void OnSettings() { if (g_pane) rp::host->ShowPane(rp::self, g_pane, 1); }

    const char* OnCommand(const char* cmd)
    {
        static std::string reply;
        if (!cmd) return nullptr;
        std::string c = cmd;
        if (c == "status")
        {
            reply = "folder=" + Utf8(g_folder) + " items=" + std::to_string(g_all.size()) + " shown=" + std::to_string(g_shown.size()) + " folders=" + std::to_string(g_folders.size())
                + " type=" + Utf8(g_type) + " selected=" + Utf8(g_selected) + " | editor " + editor::Status();
            return reply.c_str();
        }
        if (c.rfind("open ", 0) == 0) { reply = editor::Open(Wide(c.substr(5))) ? "ok" : "failed"; return reply.c_str(); }
        if (c.rfind("folder ", 0) == 0) { Navigate(Wide(c.substr(7))); reply = Utf8(g_folder); return reply.c_str(); }
        if (c.rfind("search ", 0) == 0) { Ui()->SetEditText(g_searchEdit, c.substr(7).c_str()); g_search = Wide(c.substr(7)); ShowItems(); reply = std::to_string(g_shown.size()); return reply.c_str(); }
        if (c.rfind("type ", 0) == 0) { g_type = Wide(c.substr(5)); ShowItems(); reply = std::to_string(g_shown.size()); return reply.c_str(); }
        if (c == "tiles" || c == "list") { SetMode(c == "tiles"); return "ok"; }
        if (c.rfind("new-folder ", 0) == 0 || c.rfind("new-file ", 0) == 0)
        {
            bool folder = c[4] == 'f' && c[5] == 'o';
            std::wstring error, made = folder ? pfs::NewFolder(Roots(), g_folder, Wide(c.substr(11)), error) : pfs::NewFile(Roots(), g_folder, Wide(c.substr(9)), error);
            if (made.empty()) { reply = "refused: " + Utf8(error); return reply.c_str(); }
            if (folder) BuildTree();
            g_selected = made; LoadItems();
            reply = Utf8(made); return reply.c_str();
        }
        if (c.rfind("rename ", 0) == 0)
        {
            std::string rest = c.substr(7);
            size_t bar = rest.find('|');
            if (bar == std::string::npos) return "usage: rename <path>|<new name>";
            std::wstring error, renamed = pfs::Rename(Roots(), Wide(rest.substr(0, bar)), Wide(rest.substr(bar + 1)), error);
            if (renamed.empty()) { reply = "refused: " + Utf8(error); return reply.c_str(); }
            editor::PathRenamed(Wide(rest.substr(0, bar)), renamed);
            BuildTree(); LoadItems();
            reply = Utf8(renamed); return reply.c_str();
        }
        if (c == "selftest") { reply = editor::SelfTest(); return reply.c_str(); }
        if (c == "test-open")
        {
            for (const pfs::Item& it : g_all) if (!it.folder && pfs::IsEditableText(it.fullPath)) { editor::Open(it.fullPath); reply = Utf8(it.fullPath); return reply.c_str(); }
            return "no text file in the folder";
        }
        return nullptr;
    }
}

extern "C" MODTOOLS_PLUGIN_EXPORT const ModToolsPluginApi* ModToolsPluginEntry(ModToolsPlugin* self, const ModToolsHostApi* host)
{
    rp::Bind(self, host);
    static ModToolsPluginApi a{};
    a.size = sizeof a;
    a.apiVersion = MODTOOLS_PLUGIN_API_VERSION;
    a.name = "ProjectBrowser";
    a.displayName = "Project";
    a.version = PROJECTBROWSER_VERSION;
    a.author = "Owen";
    a.description = "Radiance's Project dock and code editor in Radiant: browse the map's usermaps folder, create, rename and delete items, and edit scripts, zones and other text files.";
    a.apps = MODTOOLS_APP_RADIANT;
    a.OnLoad = OnLoad;
    a.OnUnload = OnUnload;
    a.OnTick = OnTick;
    a.OnMapLoaded = OnMapLoaded;
    a.OnCommand = OnCommand;
    a.OnSettings = OnSettings;
    return &a;
}
