#include "ProjectFs.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cwctype>
#include <set>

namespace pfs
{
    std::string Utf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(size_t(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
        return s;
    }
    std::wstring Wide(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
        std::wstring w(size_t(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
        return w;
    }
    static wchar_t Up(wchar_t c) { return c < 128 ? (c >= L'a' && c <= L'z' ? wchar_t(c - 32) : c) : wchar_t(std::towupper(c)); }
    int CompareCi(const std::wstring& a, const std::wstring& b)
    {
        size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i) { wchar_t x = Up(a[i]), y = Up(b[i]); if (x != y) return x < y ? -1 : 1; }
        return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
    }
    bool EqualsCi(const std::wstring& a, const std::wstring& b) { return a.size() == b.size() && CompareCi(a, b) == 0; }
    bool ContainsCi(const std::wstring& text, const std::wstring& term)
    {
        if (term.empty()) return true;
        for (size_t i = 0; i + term.size() <= text.size(); ++i)
        {
            size_t j = 0;
            while (j < term.size() && Up(text[i + j]) == Up(term[j])) ++j;
            if (j == term.size()) return true;
        }
        return false;
    }
    std::wstring FileName(const std::wstring& path)
    {
        size_t s = path.find_last_of(L"\\/");
        return s == std::wstring::npos ? path : path.substr(s + 1);
    }
    std::wstring Extension(const std::wstring& path)
    {
        std::wstring name = FileName(path);
        size_t dot = name.find_last_of(L'.');
        return dot == std::wstring::npos || dot == 0 ? std::wstring() : name.substr(dot);
    }
    std::wstring Parent(const std::wstring& path)
    {
        size_t s = path.find_last_of(L"\\/");
        return s == std::wstring::npos ? std::wstring() : path.substr(0, s);
    }
    std::wstring FullPath(const std::wstring& path)
    {
        if (path.empty()) return path;
        wchar_t buf[4096];
        DWORD n = GetFullPathNameW(path.c_str(), 4096, buf, nullptr);
        std::wstring full = n > 0 && n < 4096 ? std::wstring(buf, n) : path;
        while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
        return full;
    }
    bool IsDirectory(const std::wstring& path) { DWORD a = GetFileAttributesW(path.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
    bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

    // ---- folders -------------------------------------------------------------------------------------------------
    bool UserMapRoot(const std::wstring& mapPath, std::wstring& root, std::wstring& mapName)
    {
        root.clear();
        std::wstring name = FileName(mapPath);
        size_t dot = name.find_last_of(L'.');
        mapName = dot == std::wstring::npos ? name : name.substr(0, dot);
        if (mapPath.empty() || mapName.empty()) return false;
        std::wstring dir = Parent(FullPath(mapPath));
        while (!dir.empty() && !EqualsCi(FileName(dir), L"map_source"))
        {
            std::wstring up = Parent(dir);
            if (up == dir) { dir.clear(); break; }
            dir = up;
        }
        if (dir.empty() || Parent(dir).empty()) return false;
        root = Parent(dir) + L"\\usermaps\\" + mapName;
        return true;
    }

    void PopulateDirectories(Folder& parent, int depth)
    {
        if (depth >= 5 || !IsDirectory(parent.fullPath)) return;
        std::vector<std::wstring> names;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((parent.fullPath + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            names.push_back(n);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) { return CompareCi(a, b) < 0; });
        for (const std::wstring& n : names)
        {
            Folder child;
            child.name = n;
            child.fullPath = parent.fullPath + L"\\" + n;
            child.display = parent.display + L" › " + n;
            PopulateDirectories(child, depth + 1);
            parent.children.push_back(std::move(child));
        }
    }

    Folder BuildUserMapFolders(const std::wstring& mapPath)
    {
        Folder usermaps;
        std::wstring root, mapName;
        if (!UserMapRoot(mapPath, root, mapName)) return usermaps;
        usermaps.name = L"usermaps"; usermaps.fullPath = Parent(root); usermaps.display = L"usermaps"; usermaps.expanded = true;
        Folder map;
        map.name = mapName; map.fullPath = root; map.display = L"usermaps › " + mapName; map.expanded = true;
        PopulateDirectories(map, 0);
        usermaps.children.push_back(std::move(map));
        return usermaps;
    }

    // ---- items ---------------------------------------------------------------------------------------------------
    std::wstring TypeFor(const std::wstring& extension)
    {
        if (extension.empty()) return L"File";
        std::wstring t = extension.substr(1);
        for (wchar_t& c : t) c = Up(c);
        return t;
    }
    int IconFor(const std::wstring& extension)
    {
        static const wchar_t* script[] = { L".map", L".gsc", L".csc", L".txt", L".json", L".csv", L".zone" };
        static const wchar_t* image[] = { L".iwi", L".dds", L".png", L".jpg" };
        static const wchar_t* audio[] = { L".wav", L".mp3" };
        for (auto e : script) if (EqualsCi(extension, e)) return 2;
        for (auto e : image) if (EqualsCi(extension, e)) return 3;
        for (auto e : audio) if (EqualsCi(extension, e)) return 4;
        return 0;
    }

    std::vector<Item> GetItems(const std::wstring& folder, std::wstring& error)
    {
        error.clear();
        std::vector<Item> dirs, files;
        if (folder.empty() || !IsDirectory(folder)) return {};
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((folder + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
        {
            DWORD e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND) error = L"The folder could not be read (error " + std::to_wstring(e) + L").";
            return {};
        }
        do
        {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            Item it;
            it.name = n; it.fullPath = folder + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { it.folder = true; it.type = L"Folder"; it.icon = 1; dirs.push_back(it); }
            else
            {
                std::wstring ext = Extension(n);
                it.type = TypeFor(ext); it.icon = IconFor(ext);
                it.size = (uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                files.push_back(it);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        auto byName = [](const Item& a, const Item& b) { return CompareCi(a.name, b.name) < 0; };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);
        dirs.insert(dirs.end(), files.begin(), files.end());
        return dirs;
    }

    static std::wstring Trim(const std::wstring& s)
    {
        size_t a = s.find_first_not_of(L" \t\r\n"), b = s.find_last_not_of(L" \t\r\n");
        return a == std::wstring::npos ? std::wstring() : s.substr(a, b - a + 1);
    }

    std::vector<Item> Filter(const std::vector<Item>& items, const std::wstring& query, const std::wstring& type)
    {
        std::wstring term = Trim(query);
        std::vector<Item> out;
        for (const Item& it : items)
        {
            if (!term.empty() && !ContainsCi(it.name, term) && !ContainsCi(it.fullPath, term) && !ContainsCi(it.type, term)) continue;
            if (!type.empty() && !it.folder && !EqualsCi(it.type, type)) continue;
            out.push_back(it);
        }
        return out;
    }

    std::vector<std::wstring> TypeLabels(const std::vector<Item>& items)
    {
        std::vector<std::wstring> labels;
        for (const Item& it : items)
            if (!it.folder && std::none_of(labels.begin(), labels.end(), [&](const std::wstring& l) { return EqualsCi(l, it.type); })) labels.push_back(it.type);
        std::sort(labels.begin(), labels.end(), [](const std::wstring& a, const std::wstring& b) { return CompareCi(a, b) < 0; });
        return labels;
    }

    // ---- create / rename / delete ----------------------------------------------------------------------------------
    std::wstring ValidateName(const std::wstring& name, std::wstring& error)
    {
        std::wstring n = Trim(name);
        bool bad = n.empty() || n == L"." || n == L".." || n.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos;
        for (wchar_t c : n) if (c < 32) bad = true;
        if (bad) { error = L"Enter one folder or file name without a path."; return std::wstring(); }
        return n;
    }

    bool Inside(const std::wstring& root, const std::wstring& path, bool allowRoot)
    {
        std::wstring r = FullPath(root), p = FullPath(path);
        if (r.empty() || p.empty()) return false;
        if (allowRoot && EqualsCi(r, p)) return true;
        return p.size() > r.size() + 1 && EqualsCi(p.substr(0, r.size()), r) && p[r.size()] == L'\\';
    }

    static bool InAnyRoot(const std::vector<std::wstring>& roots, const std::wstring& path, bool allowRoot)
    {
        for (const std::wstring& r : roots) if (!r.empty() && Inside(r, path, allowRoot)) return true;
        return false;
    }

    static std::wstring Prepare(const std::vector<std::wstring>& roots, const std::wstring& parent, const std::wstring& name, std::wstring& error)
    {
        if (!InAnyRoot(roots, parent, true)) { error = L"Project items can only be created inside the active map's usermaps folder."; return {}; }
        std::wstring safe = ValidateName(name, error);
        if (safe.empty()) return {};
        std::wstring target = FullPath(parent + L"\\" + safe);
        if (!InAnyRoot(roots, target, false)) { error = L"Project items can only be created inside the active map's usermaps folder."; return {}; }
        if (Exists(target)) { error = L"'" + safe + L"' already exists in this folder."; return {}; }
        return target;
    }

    static void CreateDirectories(const std::wstring& path)
    {
        if (path.empty() || IsDirectory(path)) return;
        CreateDirectories(Parent(path));
        CreateDirectoryW(path.c_str(), nullptr);
    }

    std::wstring NewFolder(const std::vector<std::wstring>& roots, const std::wstring& parent, const std::wstring& name, std::wstring& error)
    {
        std::wstring target = Prepare(roots, parent, name, error);
        if (target.empty()) return {};
        CreateDirectories(FullPath(parent));
        if (!CreateDirectoryW(target.c_str(), nullptr)) { error = L"The folder could not be created (error " + std::to_wstring(GetLastError()) + L")."; return {}; }
        return target;
    }

    std::wstring NewFile(const std::vector<std::wstring>& roots, const std::wstring& parent, const std::wstring& name, std::wstring& error)
    {
        std::wstring target = Prepare(roots, parent, name, error);
        if (target.empty()) return {};
        CreateDirectories(FullPath(parent));
        HANDLE h = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { error = L"The file could not be created (error " + std::to_wstring(GetLastError()) + L")."; return {}; }
        CloseHandle(h);
        return target;
    }

    std::wstring Rename(const std::vector<std::wstring>& roots, const std::wstring& path, const std::wstring& newName, std::wstring& error)
    {
        std::wstring source = FullPath(path);
        if (!InAnyRoot(roots, source, false)) { error = L"Only items inside the project folders can be renamed."; return {}; }
        std::wstring safe = ValidateName(newName, error);
        if (safe.empty()) return {};
        std::wstring target = Parent(source) + L"\\" + safe;
        if (target == source) return source;
        if (Exists(target) && !EqualsCi(target, source)) { error = L"'" + safe + L"' already exists in this folder."; return {}; }
        if (!MoveFileExW(source.c_str(), target.c_str(), 0)) { error = L"It could not be renamed (error " + std::to_wstring(GetLastError()) + L")."; return {}; }
        return target;
    }

    bool DeleteToRecycleBin(const std::vector<std::wstring>& roots, const std::wstring& path, std::wstring& error)
    {
        std::wstring target = FullPath(path);
        if (!InAnyRoot(roots, target, false)) { error = L"Only items inside the project folders can be deleted."; return false; }
        std::wstring list = target; list.push_back(L'\0'); list.push_back(L'\0');
        SHFILEOPSTRUCTW op{};
        op.wFunc = FO_DELETE;
        op.pFrom = list.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_WANTNUKEWARNING;
        int r = SHFileOperationW(&op);
        if (r != 0 || op.fAnyOperationsAborted) { error = L"It could not be moved to the Recycle Bin (error " + std::to_wstring(r) + L")."; return false; }
        return true;
    }

    // ---- text files ----------------------------------------------------------------------------------------------
    static const wchar_t* kTextExtensions[] = {
        L".gsc", L".csc", L".gsh", L".csv", L".tsv", L".zone", L".zpkg", L".cs", L".c", L".cpp", L".h", L".hpp", L".inl", L".json", L".xml",
        L".xaml", L".axaml", L".xshd", L".gdt", L".py", L".md", L".ps1", L".js", L".html", L".htm", L".css", L".hlsl", L".hlsli", L".fx",
    };

    bool IsEditableText(const std::wstring& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        uint64_t size = (uint64_t(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
        if (size > 16ull * 1024 * 1024) return false;
        std::wstring ext = Extension(path);
        for (auto e : kTextExtensions) if (EqualsCi(ext, e)) return true;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        char buf[8192]; DWORD read = 0;
        BOOL ok = ReadFile(h, buf, sizeof buf, &read, nullptr);
        CloseHandle(h);
        if (!ok) return false;
        return std::find(buf, buf + read, '\0') == buf + read;
    }

    std::wstring LanguageName(const std::wstring& path)
    {
        std::wstring e = Extension(path);
        auto is = [&](std::initializer_list<const wchar_t*> l) { for (auto x : l) if (EqualsCi(e, x)) return true; return false; };
        if (is({ L".gsc", L".csc", L".gsh" })) return TypeFor(e);
        if (is({ L".csv", L".tsv", L".zone", L".zpkg" })) return L"CSV";
        if (is({ L".lua" })) return L"Lua";
        if (is({ L".gdt" })) return L"GDT";
        if (is({ L".cs" })) return L"C#";
        if (is({ L".c", L".cpp", L".h", L".hpp", L".inl", L".hlsl", L".hlsli", L".fx" })) return L"C++";
        if (is({ L".json" })) return L"Json";
        if (is({ L".xml", L".xaml", L".axaml", L".xshd" })) return L"XML";
        if (is({ L".py" })) return L"Python";
        if (is({ L".md" })) return L"MarkDown";
        if (is({ L".ps1" })) return L"PowerShell";
        if (is({ L".js" })) return L"JavaScript";
        if (is({ L".html", L".htm" })) return L"HTML";
        if (is({ L".css" })) return L"CSS";
        return L"Plain Text";
    }

    std::wstring EncodingLabel(Encoding e)
    {
        switch (e)
        {
        case Encoding::Utf8: return L"UTF-8";
        case Encoding::Utf8Bom: return L"UTF-8 with BOM";
        case Encoding::Utf16Le: return L"Unicode";
        case Encoding::Utf16Be: return L"Unicode (Big-Endian)";
        default: return L"Western European (ISO)";
        }
    }

    bool Decode(const std::string& b, TextFile& out)
    {
        std::wstring text;
        const unsigned char* u = reinterpret_cast<const unsigned char*>(b.data());
        if (b.size() >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) { out.encoding = Encoding::Utf8Bom; text = Wide(b.substr(3)); }
        else if (b.size() >= 2 && u[0] == 0xFF && u[1] == 0xFE)
        {
            out.encoding = Encoding::Utf16Le;
            text.assign(reinterpret_cast<const wchar_t*>(b.data() + 2), (b.size() - 2) / 2);
        }
        else if (b.size() >= 2 && u[0] == 0xFE && u[1] == 0xFF)
        {
            out.encoding = Encoding::Utf16Be;
            for (size_t i = 2; i + 1 < b.size(); i += 2) text.push_back(wchar_t((u[i] << 8) | u[i + 1]));
        }
        else
        {
            int n = b.empty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, b.data(), int(b.size()), nullptr, 0);
            if (b.empty() || n > 0) { out.encoding = Encoding::Utf8; text = Wide(b); }
            else
            {
                out.encoding = Encoding::Latin1;   // old tool files: keep every byte as it was
                text.resize(b.size());
                for (size_t i = 0; i < b.size(); ++i) text[i] = wchar_t(u[i]);
            }
        }
        out.crlf = text.find(L"\r\n") != std::wstring::npos || text.find(L'\n') == std::wstring::npos;
        std::wstring norm;
        norm.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') continue;
            norm.push_back(text[i]);
        }
        out.spaces = norm.find(L"\n    ") != std::wstring::npos && norm.find(L"\n\t") == std::wstring::npos;
        out.text.swap(norm);
        return true;
    }

    std::string Encode(const TextFile& file, const std::wstring& text)
    {
        std::wstring t;
        if (file.crlf) { t.reserve(text.size() + text.size() / 20); for (wchar_t c : text) { if (c == L'\n') t += L"\r\n"; else t.push_back(c); } }
        else t = text;
        std::string out;
        switch (file.encoding)
        {
        case Encoding::Utf8Bom: out = "\xEF\xBB\xBF" + Utf8(t); break;
        case Encoding::Utf16Le:
            out = "\xFF\xFE";
            out.append(reinterpret_cast<const char*>(t.data()), t.size() * 2);
            break;
        case Encoding::Utf16Be:
            out = "\xFE\xFF";
            for (wchar_t c : t) { out.push_back(char((c >> 8) & 0xFF)); out.push_back(char(c & 0xFF)); }
            break;
        case Encoding::Latin1:
            out.resize(t.size());
            for (size_t i = 0; i < t.size(); ++i) out[i] = t[i] < 256 ? char(t[i]) : '?';
            break;
        default: out = Utf8(t); break;
        }
        return out;
    }

    uint64_t WriteTime(const std::wstring& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return 0;
        return (uint64_t(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
    }

    bool LoadText(const std::wstring& path, TextFile& out, std::wstring& error)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { error = L"Could not open the file (error " + std::to_wstring(GetLastError()) + L")."; return false; }
        LARGE_INTEGER size{};
        GetFileSizeEx(h, &size);
        if (size.QuadPart > 16LL * 1024 * 1024) { CloseHandle(h); error = L"The file is larger than 16 MB."; return false; }
        std::string bytes(size_t(size.QuadPart), '\0');
        DWORD read = 0;
        BOOL ok = bytes.empty() || ReadFile(h, &bytes[0], DWORD(bytes.size()), &read, nullptr);
        CloseHandle(h);
        if (!ok) { error = L"Could not read the file."; return false; }
        bytes.resize(read);
        Decode(bytes, out);
        out.writeTime = WriteTime(path);
        return true;
    }

    bool SaveText(const std::wstring& path, TextFile& file, const std::wstring& text, std::wstring& error)
    {
        std::string bytes = Encode(file, text);
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { error = L"Could not save (error " + std::to_wstring(GetLastError()) + L")."; return false; }
        DWORD written = 0;
        BOOL ok = bytes.empty() || WriteFile(h, bytes.data(), DWORD(bytes.size()), &written, nullptr);
        CloseHandle(h);
        if (!ok || written != bytes.size()) { error = L"Could not save: the write was incomplete."; return false; }
        file.writeTime = WriteTime(path);
        return true;
    }
}
