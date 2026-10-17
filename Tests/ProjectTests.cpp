// Unit checks of ProjectFs (Radiance's UserMapProjectBrowser and the code editor's load / save) and the highlighter.
#include "../ProjectFs.h"
#include "../Highlighter.h"
#include <windows.h>
#include <cstdio>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void Write(const std::wstring& path, const std::string& bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n = 0; WriteFile(h, bytes.data(), DWORD(bytes.size()), &n, nullptr); CloseHandle(h);
}
static std::string Read(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    char buf[4096]; DWORD n = 0; ReadFile(h, buf, sizeof buf, &n, nullptr); CloseHandle(h);
    return std::string(buf, n);
}
static bool HasSpan(const std::vector<hl::Span>& spans, int start, int length, uint32_t color)
{
    for (auto& s : spans) if (s.start == start && s.length == length && s.color == color) return true;
    return false;
}

int main(int argc, char** argv)
{
    using namespace pfs;
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring base = std::wstring(tmp) + L"ProjectBrowserTests";
    if (argc > 1) base = Wide(argv[1]);
    CreateDirectoryW(base.c_str(), nullptr);
    std::wstring src = base + L"\\map_source", sub = src + L"\\zm", user = base + L"\\usermaps", mapRoot = user + L"\\zm_test";
    for (auto d : { src, sub, user, mapRoot, mapRoot + L"\\scripts", mapRoot + L"\\scripts\\zm", mapRoot + L"\\zone_source" }) CreateDirectoryW(d.c_str(), nullptr);

    // ---- usermap root ----
    std::wstring root, name;
    CHECK(UserMapRoot(sub + L"\\zm_test.map", root, name));
    CHECK(EqualsCi(root, mapRoot) && name == L"zm_test");
    CHECK(!UserMapRoot(base + L"\\elsewhere\\x.map", root, name));
    Folder f = BuildUserMapFolders(sub + L"\\zm_test.map");
    CHECK(f.name == L"usermaps" && f.expanded && f.children.size() == 1);
    CHECK(f.children[0].name == L"zm_test" && f.children[0].display == L"usermaps › zm_test");
    CHECK(f.children[0].children.size() == 2 && f.children[0].children[0].name == L"scripts" && f.children[0].children[1].name == L"zone_source");
    CHECK(f.children[0].children[0].children[0].display == L"usermaps › zm_test › scripts › zm");

    // ---- items, filters ----
    Write(mapRoot + L"\\zone_source\\zm_test.zone", "scriptparsetree,scripts/zm/zm_test.gsc\n");
    Write(mapRoot + L"\\zone_source\\b.csv", "a,1\n");
    Write(mapRoot + L"\\zone_source\\README", "x");
    CreateDirectoryW((mapRoot + L"\\zone_source\\Sub").c_str(), nullptr);
    std::wstring error;
    auto items = GetItems(mapRoot + L"\\zone_source", error);
    CHECK(error.empty() && items.size() == 4);
    CHECK(items[0].folder && items[0].name == L"Sub" && items[0].type == L"Folder");
    CHECK(items[1].name == L"b.csv" && items[1].type == L"CSV" && items[1].icon == 2);
    CHECK(items[2].name == L"README" && items[2].type == L"File");
    CHECK(items[3].name == L"zm_test.zone" && items[3].type == L"ZONE");
    CHECK(Filter(items, L"zone", L"").size() == 4);                        // every full path contains "zone_source"
    CHECK(Filter(items, L"B.CSV", L"").size() == 1);
    CHECK(Filter(items, L"", L"CSV").size() == 2);                         // the folder stays for navigation
    CHECK(Filter(items, L"  csv ", L"").size() == 1);
    auto labels = TypeLabels(items);
    CHECK(labels.size() == 3 && labels[0] == L"CSV" && labels[1] == L"File" && labels[2] == L"ZONE");

    // ---- create / rename / delete, only inside the roots ----
    std::vector<std::wstring> roots{ mapRoot };
    CHECK(NewFolder(roots, mapRoot + L"\\scripts", L"..", error).empty() && !error.empty());
    CHECK(NewFolder(roots, mapRoot + L"\\scripts", L"a\\b", error).empty());
    CHECK(NewFolder(roots, user, L"outside", error).empty());              // the usermaps folder itself is not the project
    std::wstring made = NewFolder(roots, mapRoot + L"\\scripts", L" shared ", error);
    CHECK(!made.empty() && IsDirectory(mapRoot + L"\\scripts\\shared"));
    CHECK(NewFolder(roots, mapRoot + L"\\scripts", L"shared", error).empty() && error.find(L"already exists") != std::wstring::npos);
    std::wstring file = NewFile(roots, mapRoot + L"\\scripts\\shared", L"util.gsc", error);
    CHECK(!file.empty() && Exists(file));
    CHECK(NewFile(roots, mapRoot + L"\\scripts\\shared", L"util.gsc", error).empty());
    std::wstring renamed = Rename(roots, file, L"helpers.gsc", error);
    CHECK(EqualsCi(renamed, mapRoot + L"\\scripts\\shared\\helpers.gsc") && Exists(renamed) && !Exists(file));
    CHECK(Rename(roots, mapRoot, L"other", error).empty());                // never the project root
    CHECK(!DeleteToRecycleBin(roots, mapRoot, error));
    CHECK(Inside(mapRoot, mapRoot + L"\\a", false) && !Inside(mapRoot, mapRoot + L"x\\a", false) && Inside(mapRoot, mapRoot, true));

    // ---- text: encodings and line endings survive a save ----
    TextFile t;
    CHECK(Decode("\xEF\xBB\xBF" "a\r\nb\r\n", t) && t.encoding == Encoding::Utf8Bom && t.crlf && t.text == L"a\nb\n");
    CHECK(Encode(t, L"a\nc\n") == "\xEF\xBB\xBF" "a\r\nc\r\n");
    CHECK(Decode("x\ny", t) && t.encoding == Encoding::Utf8 && !t.crlf && Encode(t, L"x\nz") == "x\nz");
    CHECK(Decode("caf\xE9\n", t) && t.encoding == Encoding::Latin1 && t.text == L"café\n" && Encode(t, t.text) == "caf\xE9\n");
    CHECK(Decode(std::string("\xFF\xFE" "h\0i\0\r\0\n\0", 10), t) && t.encoding == Encoding::Utf16Le && t.text == L"hi\n" && t.crlf);
    CHECK(Decode("one line", t) && t.crlf);                                // no line break: CRLF like Radiance
    CHECK(Decode("a\n    b\n", t) && t.spaces);
    CHECK(Decode("a\n\tb\n    c\n", t) && !t.spaces);
    TextFile disk;
    std::wstring path = mapRoot + L"\\scripts\\zm\\zm_test.gsc";
    Write(path, "\xEF\xBB\xBF" "function main()\r\n{\r\n}\r\n");
    CHECK(LoadText(path, disk, error) && disk.text == L"function main()\n{\n}\n" && disk.writeTime != 0);
    CHECK(SaveText(path, disk, disk.text + L"// end\n", error));
    CHECK(Read(path) == "\xEF\xBB\xBF" "function main()\r\n{\r\n}\r\n// end\r\n");
    CHECK(IsEditableText(path) && IsEditableText(mapRoot + L"\\zone_source\\README"));
    Write(mapRoot + L"\\bin.dat", std::string("a\0b", 3));
    CHECK(!IsEditableText(mapRoot + L"\\bin.dat"));
    CHECK(LanguageName(L"x.gsc") == L"GSC" && LanguageName(L"x.CSC") == L"CSC" && LanguageName(L"x.zone") == L"CSV" && LanguageName(L"x.gdt") == L"GDT" && LanguageName(L"x.txt") == L"Plain Text");

    // ---- highlighting ----
    using namespace hl;
    CHECK(LanguageFor(L"a\\b.gsc") == Lang::Gsc && LanguageFor(L"x.zone") == Lang::Csv && LanguageFor(L"x.gdt") == Lang::CLike && LanguageFor(L"x.json") == Lang::Json && LanguageFor(L"x.txt") == Lang::None);
    std::vector<Span> s;
    std::wstring line = L"function main() { self thread zm_util::foo(\"hi\\n\", 1.5); } // done";
    CHECK(HighlightLine(Lang::Gsc, line.c_str(), int(line.size()), 0, s) == 0);
    CHECK(HasSpan(s, 0, 8, 0x569CD6));                                     // function: keyword
    CHECK(HasSpan(s, 9, 4, 0xDCDCAA));                                     // main(: call
    CHECK(HasSpan(s, 18, 4, 0x4FC1FF));                                    // self: entity
    CHECK(HasSpan(s, 23, 6, 0xC586C0));                                    // thread: control
    CHECK(HasSpan(s, 30, 7, 0x4EC9B0));                                    // zm_util: namespace
    CHECK(HasSpan(s, 39, 3, 0xDCDCAA));                                    // ::foo: function pointer
    CHECK(HasSpan(s, 43, 3, 0xCE9178) && HasSpan(s, 46, 2, 0xD7BA7D));      // "hi then the \n escape
    CHECK(HasSpan(s, 51, 3, 0xB5CEA8));                                    // 1.5
    CHECK(HasSpan(s, 59, 7, 0x6A9955));                                    // comment
    std::wstring open = L"x = 1; /* starts";
    CHECK(HighlightLine(Lang::Gsc, open.c_str(), int(open.size()), 0, s) == 1);
    std::wstring mid = L"still comment */ wait 1;";
    CHECK(HighlightLine(Lang::Gsc, mid.c_str(), int(mid.size()), 1, s) == 0 && HasSpan(s, 0, 16, 0x6A9955) && HasSpan(s, 17, 4, 0xC586C0));
    std::wstring doc = L"/@ doc";
    CHECK(HighlightLine(Lang::Gsc, doc.c_str(), int(doc.size()), 0, s) == 2);
    std::wstring pre = L"#using scripts\\shared\\util;";
    HighlightLine(Lang::Gsc, pre.c_str(), int(pre.size()), 0, s);
    CHECK(HasSpan(s, 0, 6, 0xC586C0));
    std::wstring loc = L"&\"ZM_HINT\" #\"hash\"";
    HighlightLine(Lang::Gsc, loc.c_str(), int(loc.size()), 0, s);
    CHECK(HasSpan(s, 0, 10, 0xD7BA7D) && HasSpan(s, 11, 7, 0xD7BA7D));
    std::wstring csv = L"scriptparsetree,scripts/zm/a.gsc,12, true";
    HighlightLine(Lang::Csv, csv.c_str(), int(csv.size()), 0, s);
    CHECK(HasSpan(s, 0, 15, 0x9CDCFE) && HasSpan(s, 15, 1, 0x808080) && HasSpan(s, 33, 2, 0xB5CEA8) && HasSpan(s, 37, 4, 0x569CD6));
    std::wstring csvComment = L"  // all of it";
    HighlightLine(Lang::Csv, csvComment.c_str(), int(csvComment.size()), 0, s);
    CHECK(s.size() == 1 && s[0].length == int(csvComment.size()));
    std::wstring json = L"{ \"name\": \"x\", \"n\": -2, \"ok\": true }";
    HighlightLine(Lang::Json, json.c_str(), int(json.size()), 0, s);
    CHECK(HasSpan(s, 2, 6, 0x9CDCFE) && HasSpan(s, 10, 3, 0xCE9178) && HasSpan(s, 20, 2, 0xB5CEA8) && HasSpan(s, 30, 4, 0x569CD6));

    auto words = Completions(Lang::Gsc, L"function zm_spawner() { zm_spawn_thing(); }", L"zm_sp", 10);
    CHECK(words.size() == 2 && words[0] == L"zm_spawn_thing" && words[1] == L"zm_spawner");
    CHECK(Completions(Lang::Gsc, L"", L"waittill", 10).size() == 3);          // waittill, waittillframeend, waittillmatch
    auto edits = ToggleLineComments(L"a\n  b\n\nc", 0, 2);
    CHECK(edits.size() == 2 && edits[0].offset == 4 && edits[0].insert == L"// " && edits[1].offset == 0);
    auto undo = ToggleLineComments(L"// a\n  //b", 0, 1);
    CHECK(undo.size() == 2 && undo[0].offset == 7 && undo[0].remove == 2 && undo[1].offset == 0 && undo[1].remove == 3);

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
