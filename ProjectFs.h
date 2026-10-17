// ProjectFs: the Project pane's file system side, ported from Radiance (Editor\Services\UserMapProjectBrowser.cs, the
// project part of MainWindowViewModel.cs and CodeEditorView's load/save). No UI, unit tested.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pfs
{
    std::string Utf8(const std::wstring& w);
    std::wstring Wide(const std::string& s);
    bool EqualsCi(const std::wstring& a, const std::wstring& b);
    bool ContainsCi(const std::wstring& text, const std::wstring& term);
    int CompareCi(const std::wstring& a, const std::wstring& b);
    std::wstring FileName(const std::wstring& path);
    std::wstring Extension(const std::wstring& path);        // ".gsc", "" when none
    std::wstring Parent(const std::wstring& path);
    std::wstring FullPath(const std::wstring& path);         // normalized, no trailing separator
    bool IsDirectory(const std::wstring& path);
    bool Exists(const std::wstring& path);

    struct Folder
    {
        std::wstring name, fullPath, display;   // display: "usermaps › mymap › scripts"
        bool expanded = false;
        std::vector<Folder> children;
    };

    // TryGetUserMapRoot: <...>\map_source\[sub\]name.map -> <...>\usermaps\name (false without a map_source ancestor).
    bool UserMapRoot(const std::wstring& mapPath, std::wstring& root, std::wstring& mapName);
    // BuildFoldersForMap: "usermaps" (expanded) > the map's folder (expanded) > its directories, 5 levels deep,
    // reparse points skipped. Empty name when the map is not inside a project.
    Folder BuildUserMapFolders(const std::wstring& mapPath);
    void PopulateDirectories(Folder& parent, int depth);

    struct Item
    {
        std::wstring name, fullPath, type;   // type: "Folder", "File", or the extension upper-cased ("GSC")
        bool folder = false;
        int icon = 0;                        // 0 file, 1 folder, 2 script/text, 3 image, 4 audio
        uint64_t size = 0;
    };
    std::wstring TypeFor(const std::wstring& extension);
    int IconFor(const std::wstring& extension);
    // GetItems: directories, then files, each sorted by name ignoring case. error is set when the folder cannot be read.
    std::vector<Item> GetItems(const std::wstring& folder, std::wstring& error);
    // FilterItems + the type filter: search in name, full path and type; folders always pass the type filter.
    std::vector<Item> Filter(const std::vector<Item>& items, const std::wstring& query, const std::wstring& type);
    std::vector<std::wstring> TypeLabels(const std::vector<Item>& items);   // the file types present, sorted

    // Create / rename / delete inside one of the allowed roots. Return the new path, or "" with `error` set.
    std::wstring ValidateName(const std::wstring& name, std::wstring& error);
    bool Inside(const std::wstring& root, const std::wstring& path, bool allowRoot);
    std::wstring NewFolder(const std::vector<std::wstring>& roots, const std::wstring& parent, const std::wstring& name, std::wstring& error);
    std::wstring NewFile(const std::vector<std::wstring>& roots, const std::wstring& parent, const std::wstring& name, std::wstring& error);
    std::wstring Rename(const std::vector<std::wstring>& roots, const std::wstring& path, const std::wstring& newName, std::wstring& error);
    bool DeleteToRecycleBin(const std::vector<std::wstring>& roots, const std::wstring& path, std::wstring& error);

    // ---- text files (CodeEditorView) ---------------------------------------------------------------------------
    // IsEditableText: known text extensions, or no NUL byte in the first 8 KB; at most 16 MB.
    bool IsEditableText(const std::wstring& path);
    std::wstring LanguageName(const std::wstring& path);     // "GSC", "CSC", "CSV", "GDT", "Json", "Plain Text"...

    enum class Encoding { Utf8, Utf8Bom, Utf16Le, Utf16Be, Latin1 };
    struct TextFile
    {
        std::wstring text;          // line breaks as '\n'
        Encoding encoding = Encoding::Utf8;
        bool crlf = true;           // the file's line ending (CRLF when it has none)
        bool spaces = false;        // indented with 4 spaces rather than tabs
        uint64_t writeTime = 0;
    };
    std::wstring EncodingLabel(Encoding e);
    bool Decode(const std::string& bytes, TextFile& out);
    std::string Encode(const TextFile& file, const std::wstring& text);   // with the original encoding and line endings
    bool LoadText(const std::wstring& path, TextFile& out, std::wstring& error);
    bool SaveText(const std::wstring& path, TextFile& file, const std::wstring& text, std::wstring& error);
    uint64_t WriteTime(const std::wstring& path);            // 0 when the file is gone
}
