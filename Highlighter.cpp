#include "Highlighter.h"
#include <algorithm>
#include <cwctype>
#include <set>

namespace hl
{
    namespace
    {
        // GSC.xshd colours
        constexpr uint32_t Comment = 0x6A9955, DocComment = 0x608B4E, String = 0xCE9178, LocalizedString = 0xD7BA7D, HashString = 0xD7BA7D,
            Escape = 0xD7BA7D, Number = 0xB5CEA8, Preprocessor = 0xC586C0, Keyword = 0x569CD6, Control = 0xC586C0, Entity = 0x4FC1FF,
            Constant = 0x569CD6, Namespace = 0x4EC9B0, FunctionCall = 0xDCDCAA, FunctionPointer = 0xDCDCAA;
        // CSV.xshd colours
        constexpr uint32_t CsvSeparator = 0x808080, CsvFirst = 0x9CDCFE, CsvHash = 0xD7BA7D;
        // C-like / JSON
        constexpr uint32_t CPreprocessor = 0x9B9B9B, JsonKey = 0x9CDCFE;

        wchar_t Lo(wchar_t c) { return c < 128 ? (c >= L'A' && c <= L'Z' ? wchar_t(c + 32) : c) : wchar_t(std::towlower(c)); }
        bool IdStart(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_'; }
        bool IdChar(wchar_t c) { return IdStart(c) || (c >= L'0' && c <= L'9'); }
        bool Digit(wchar_t c) { return c >= L'0' && c <= L'9'; }
        bool HexDigit(wchar_t c) { return Digit(c) || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'); }
        std::wstring Lower(const wchar_t* s, int n) { std::wstring r(s, size_t(n)); for (wchar_t& c : r) c = Lo(c); return r; }
        bool StartsWith(const wchar_t* t, int n, int i, const wchar_t* p)
        {
            for (int k = 0; p[k]; ++k) if (i + k >= n || t[i + k] != p[k]) return false;
            return true;
        }
        bool In(const std::wstring& w, std::initializer_list<const wchar_t*> list) { for (auto x : list) if (w == x) return true; return false; }

        void Add(std::vector<Span>& out, int start, int length, uint32_t color) { if (length > 0) out.push_back({ start, length, color }); }

        // A number at i (after a non-identifier character): 0x.., digits[.digits][e[+-]d], .digits. Returns its length.
        int NumberAt(const wchar_t* t, int n, int i)
        {
            if (i > 0 && IdChar(t[i - 1])) return 0;
            int j = i;
            if (t[j] == L'0' && j + 2 < n + 1 && j + 1 < n && (t[j + 1] == L'x' || t[j + 1] == L'X') && j + 2 < n && HexDigit(t[j + 2]))
            {
                j += 2; while (j < n && HexDigit(t[j])) ++j;
                return j < n && IdChar(t[j]) ? 0 : j - i;
            }
            if (Digit(t[j])) { while (j < n && Digit(t[j])) ++j; if (j < n && t[j] == L'.') { ++j; while (j < n && Digit(t[j])) ++j; } }
            else if (t[j] == L'.' && j + 1 < n && Digit(t[j + 1])) { ++j; while (j < n && Digit(t[j])) ++j; }
            else return 0;
            if (j < n && (t[j] == L'e' || t[j] == L'E'))
            {
                int k = j + 1;
                if (k < n && (t[k] == L'+' || t[k] == L'-')) ++k;
                if (k < n && Digit(t[k])) { j = k; while (j < n && Digit(t[j])) ++j; }
            }
            if (j < n && IdChar(t[j])) return 0;
            return j - i;
        }

        // A quoted string from i (t[i] is the quote, or the quote follows a one-char prefix); escapes coloured separately.
        int StringAt(const wchar_t* t, int n, int i, int quoteAt, uint32_t color, bool escapes, std::vector<Span>& out)
        {
            wchar_t q = t[quoteAt];
            int j = quoteAt + 1, runStart = i;
            while (j < n && t[j] != q)
            {
                if (escapes && t[j] == L'\\' && j + 1 < n)
                {
                    Add(out, runStart, j - runStart, color);
                    Add(out, j, 2, Escape);
                    j += 2; runStart = j;
                    continue;
                }
                ++j;
            }
            if (j < n) ++j;   // closing quote
            Add(out, runStart, j - runStart, color);
            return j;
        }

        // A block comment from i (`open` = the length of its opening marker there, 0 when it continues from the line
        // before). Returns where it ends; `state` is cleared when it closes on this line and kept when it runs on.
        int Block(const wchar_t* t, int n, int i, int open, const wchar_t* end, uint32_t color, int& state, std::vector<Span>& out)
        {
            int j = i + open;
            while (j < n && !StartsWith(t, n, j, end)) ++j;
            if (j < n) { Add(out, i, j + 2 - i, color); state = 0; return j + 2; }
            Add(out, i, n - i, color);
            return n;
        }

        int Gsc(const wchar_t* t, int n, int state, std::vector<Span>& out)
        {
            int i = 0;
            if (state) { i = Block(t, n, 0, 0, state == 2 ? L"@/" : L"*/", state == 2 ? DocComment : Comment, state, out); if (state) return state; }
            while (i < n)
            {
                wchar_t c = t[i];
                if (c == L'/' && i + 1 < n && t[i + 1] == L'@') { int st = 2; i = Block(t, n, i, 2, L"@/", DocComment, st, out); if (st) return st; continue; }
                if (c == L'/' && i + 1 < n && t[i + 1] == L'*') { int st = 1; i = Block(t, n, i, 2, L"*/", Comment, st, out); if (st) return st; continue; }
                if (c == L'/' && i + 1 < n && t[i + 1] == L'/') { Add(out, i, n - i, Comment); return 0; }
                if (c == L'&' && i + 1 < n && t[i + 1] == L'"') { i = StringAt(t, n, i, i + 1, LocalizedString, true, out); continue; }
                if (c == L'#' && i + 1 < n && t[i + 1] == L'"') { i = StringAt(t, n, i, i + 1, HashString, true, out); continue; }
                if (c == L'"' || c == L'\'') { i = StringAt(t, n, i, i, String, true, out); continue; }
                if (c == L'#')
                {
                    int j = i + 1; while (j < n && (t[j] == L' ' || t[j] == L'\t')) ++j;
                    int k = j; while (k < n && IdChar(t[k])) ++k;
                    std::wstring w = Lower(t + j, k - j);
                    if (In(w, { L"using", L"using_animtree", L"insert", L"define", L"undef", L"namespace", L"precache", L"if", L"ifdef", L"ifndef", L"elif", L"else", L"endif", L"animtree" }))
                    { Add(out, i, k - i, Preprocessor); i = k; continue; }
                    ++i; continue;
                }
                if (IdStart(c) && (i == 0 || !IdChar(t[i - 1])))
                {
                    int k = i; while (k < n && IdChar(t[k])) ++k;
                    std::wstring w = Lower(t + i, k - i);
                    uint32_t color = 0;
                    if (In(w, { L"if", L"else", L"for", L"foreach", L"in", L"while", L"do", L"switch", L"case", L"default", L"break", L"continue", L"return",
                                L"wait", L"waitrealtime", L"waittill", L"waittillmatch", L"waittillframeend", L"endon", L"notify", L"thread", L"childthread", L"goto" })) color = Control;
                    else if (In(w, { L"function", L"autoexec", L"private", L"class", L"var", L"const", L"constructor", L"destructor", L"new", L"vararg", L"local",
                                     L"static", L"delete", L"isdefined", L"size" })) color = Keyword;
                    else if (In(w, { L"self", L"level", L"game", L"anim", L"world", L"classes" })) color = Entity;
                    else if (In(w, { L"true", L"false", L"undefined" })) color = Constant;
                    else
                    {
                        int b = i - 1; while (b >= 0 && (t[b] == L' ' || t[b] == L'\t')) --b;
                        int a = k; while (a < n && (t[a] == L' ' || t[a] == L'\t')) ++a;
                        if (b >= 1 && t[b] == L':' && t[b - 1] == L':') color = FunctionPointer;
                        else if (k + 1 < n && t[k] == L':' && t[k + 1] == L':') color = Namespace;
                        else if (a < n && t[a] == L'(') color = FunctionCall;
                    }
                    if (color) Add(out, i, k - i, color);
                    i = k; continue;
                }
                if (Digit(c) || (c == L'.' && i + 1 < n && Digit(t[i + 1])))
                {
                    int len = NumberAt(t, n, i);
                    if (len > 0) { Add(out, i, len, Number); i += len; continue; }
                }
                ++i;
            }
            return 0;
        }

        int Csv(const wchar_t* t, int n, std::vector<Span>& out)
        {
            int i = 0; while (i < n && (t[i] == L' ' || t[i] == L'\t')) ++i;
            if (StartsWith(t, n, i, L"//") || (i + 1 < n && t[i] == L'#' && (t[i + 1] == L' ' || t[i + 1] == L'\t'))) { Add(out, 0, n, Comment); return 0; }
            // the first field, when a separator follows it
            int f = 0; while (f < n && t[f] != L',' && t[f] != L'\t' && t[f] != L'"') ++f;
            int pos = 0;
            if (f > 0 && f < n && (t[f] == L',' || t[f] == L'\t')) { Add(out, 0, f, CsvFirst); pos = f; }
            bool fieldStart = pos == 0;
            for (i = pos; i < n;)
            {
                wchar_t c = t[i];
                if (c == L'"') { i = StringAt(t, n, i, i, String, false, out); fieldStart = false; continue; }
                if (c == L',' || c == L'\t') { Add(out, i, 1, CsvSeparator); ++i; fieldStart = true; continue; }
                if (fieldStart)
                {
                    // a field that is only a number
                    int j = i; while (j < n && t[j] == L' ') ++j;
                    int s = j; if (j < n && t[j] == L'-') ++j;
                    int len = j < n ? NumberAt(t, n, j) : 0;
                    int e = j + len; while (e < n && t[e] == L' ') ++e;
                    if (len > 0 && (e == n || t[e] == L',' || t[e] == L'\t')) { Add(out, s, j + len - s, Number); i = e; fieldStart = false; continue; }
                }
                fieldStart = false;
                if (c == L'#' && i + 1 < n && IdChar(t[i + 1])) { int k = i + 1; while (k < n && IdChar(t[k])) ++k; Add(out, i, k - i, CsvHash); i = k; continue; }
                if (IdStart(c) && (i == 0 || !IdChar(t[i - 1])))
                {
                    int k = i; while (k < n && IdChar(t[k])) ++k;
                    std::wstring w = Lower(t + i, k - i);
                    if (In(w, { L"true", L"false", L"undefined" })) Add(out, i, k - i, Constant);
                    i = k; continue;
                }
                ++i;
            }
            return 0;
        }

        int CLike(const wchar_t* t, int n, int state, bool json, std::vector<Span>& out)
        {
            int i = 0;
            if (state) { i = Block(t, n, 0, 0, L"*/", Comment, state, out); if (state) return state; }
            int firstNonSpace = 0; while (firstNonSpace < n && (t[firstNonSpace] == L' ' || t[firstNonSpace] == L'\t')) ++firstNonSpace;
            while (i < n)
            {
                wchar_t c = t[i];
                if (c == L'/' && i + 1 < n && t[i + 1] == L'*') { int st = 1; i = Block(t, n, i, 2, L"*/", Comment, st, out); if (st) return st; continue; }
                if (c == L'/' && i + 1 < n && t[i + 1] == L'/') { Add(out, i, n - i, Comment); return 0; }
                if (c == L'"' || (!json && c == L'\''))
                {
                    int end = StringAt(t, n, i, i, String, true, out);
                    if (json)
                    {
                        int a = end; while (a < n && (t[a] == L' ' || t[a] == L'\t')) ++a;
                        if (a < n && t[a] == L':')   // a property name
                            for (auto it = out.rbegin(); it != out.rend() && it->start >= i; ++it) if (it->color == String) it->color = JsonKey;
                    }
                    i = end; continue;
                }
                if (!json && c == L'#' && i == firstNonSpace) { int k = i + 1; while (k < n && (IdChar(t[k]) || t[k] == L' ')) ++k; Add(out, i, k - i, CPreprocessor); i = k; continue; }
                if (IdStart(c) && (i == 0 || !IdChar(t[i - 1])))
                {
                    int k = i; while (k < n && IdChar(t[k])) ++k;
                    std::wstring w(t + i, size_t(k - i));
                    bool kw = json ? In(w, { L"true", L"false", L"null" })
                        : In(w, { L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"default", L"break", L"continue", L"return", L"goto",
                                  L"struct", L"class", L"enum", L"union", L"typedef", L"namespace", L"using", L"public", L"private", L"protected", L"static",
                                  L"const", L"constexpr", L"virtual", L"override", L"inline", L"extern", L"void", L"int", L"float", L"double", L"char", L"bool",
                                  L"unsigned", L"signed", L"long", L"short", L"auto", L"true", L"false", L"nullptr", L"null", L"new", L"delete", L"this",
                                  L"template", L"typename", L"var", L"let", L"function", L"string", L"float2", L"float3", L"float4", L"uniform", L"sampler" });
                    if (kw) Add(out, i, k - i, Keyword);
                    else if (!json) { int a = k; while (a < n && t[a] == L' ') ++a; if (a < n && t[a] == L'(') Add(out, i, k - i, FunctionCall); }
                    i = k; continue;
                }
                if (Digit(c) || (c == L'-' && json && i + 1 < n && Digit(t[i + 1])) || (c == L'.' && i + 1 < n && Digit(t[i + 1])))
                {
                    int s = i; if (c == L'-') ++i;
                    int len = NumberAt(t, n, i);
                    if (len > 0) { Add(out, s, i + len - s, Number); i += len; continue; }
                    i = s + 1; continue;
                }
                ++i;
            }
            return 0;
        }

        const wchar_t* kGscWords[] = {
            L"function", L"autoexec", L"private", L"class", L"var", L"const", L"constructor", L"destructor", L"new", L"vararg",
            L"if", L"else", L"for", L"foreach", L"in", L"while", L"do", L"switch", L"case", L"default", L"break", L"continue", L"return",
            L"wait", L"waitrealtime", L"waittill", L"waittillmatch", L"waittillframeend", L"endon", L"notify", L"thread", L"childthread",
            L"self", L"level", L"game", L"anim", L"world", L"true", L"false", L"undefined", L"isdefined", L"#using", L"#insert", L"#define",
            L"#namespace", L"#precache", L"IsDefined", L"IsAlive", L"IsPlayer", L"GetEnt", L"GetEntArray", L"GetStruct", L"GetStructArray",
            L"Spawn", L"SpawnStruct", L"PlayFX", L"PlayFXOnTag", L"PlaySound", L"PlaySoundAtPosition", L"IPrintLn", L"IPrintLnBold",
            L"GetPlayers", L"RandomInt", L"RandomFloat", L"RandomIntRange", L"RandomFloatRange", L"Distance", L"DistanceSquared",
            L"VectorNormalize", L"AnglesToForward", L"AnglesToRight", L"AnglesToUp", L"Int", L"Float", L"GetTime", L"SetModel",
            L"MoveTo", L"RotateTo", L"Hide", L"Show", L"Delete", L"Link", L"Unlink", L"SetCursorHint", L"SetHintString", L"TriggerEnable",
            L"array::add", L"array::remove", L"array::randomize", L"callback::on_spawned", L"callback::on_connect",
            L"clientfield::register", L"clientfield::set", L"flag::init", L"flag::set", L"flag::wait_till", L"util::wait_network_frame",
        };
    }

    Lang LanguageFor(const std::wstring& path)
    {
        size_t dot = path.find_last_of(L'.'), slash = path.find_last_of(L"\\/");
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return Lang::None;
        std::wstring e = Lower(path.c_str() + dot, int(path.size() - dot));
        if (In(e, { L".gsc", L".csc", L".gsh" })) return Lang::Gsc;
        if (In(e, { L".csv", L".tsv", L".zone", L".zpkg" })) return Lang::Csv;
        if (In(e, { L".json" })) return Lang::Json;
        if (In(e, { L".gdt", L".c", L".cpp", L".h", L".hpp", L".inl", L".hlsl", L".hlsli", L".fx", L".cs", L".js", L".css" })) return Lang::CLike;
        return Lang::None;
    }

    int HighlightLine(Lang lang, const wchar_t* text, int length, int stateIn, std::vector<Span>& out)
    {
        out.clear();
        switch (lang)
        {
        case Lang::Gsc: return Gsc(text, length, stateIn < 0 ? 0 : stateIn, out);
        case Lang::Csv: return Csv(text, length, out);
        case Lang::CLike: return CLike(text, length, stateIn == 1 ? 1 : 0, false, out);
        case Lang::Json: return CLike(text, length, 0, true, out);
        default: return 0;
        }
    }

    std::vector<std::wstring> Completions(Lang lang, const std::wstring& text, const std::wstring& prefix, size_t limit)
    {
        struct LessCi
        {
            bool operator()(const std::wstring& a, const std::wstring& b) const
            {
                size_t n = std::min(a.size(), b.size());
                for (size_t i = 0; i < n; ++i) { wchar_t x = Lo(a[i]), y = Lo(b[i]); if (x != y) return x < y; }
                return a.size() < b.size();
            }
        };
        std::set<std::wstring, LessCi> words;
        if (lang == Lang::Gsc) for (auto w : kGscWords) words.insert(w);
        const wchar_t* t = text.c_str();
        size_t n = text.size();
        for (size_t i = 0; i < n;)
        {
            if (!IdStart(t[i]) || (i > 0 && IdChar(t[i - 1]))) { ++i; continue; }
            size_t k = i; while (k < n && IdChar(t[k])) ++k;
            if (k + 2 < n && t[k] == L':' && t[k + 1] == L':' && IdStart(t[k + 2])) { k += 2; while (k < n && IdChar(t[k])) ++k; }
            std::wstring w = text.substr(i, k - i);
            if (w.size() >= 3 && w != prefix) words.insert(w);
            i = k;
        }
        std::vector<std::wstring> out;
        std::wstring p = Lower(prefix.c_str(), int(prefix.size()));
        for (const std::wstring& w : words)
        {
            if (out.size() >= limit) break;
            if (p.empty() || Lower(w.c_str(), int(std::min(w.size(), p.size()))) == p) out.push_back(w);
        }
        return out;
    }

    std::vector<Edit> ToggleLineComments(const std::wstring& text, int firstLine, int lastLine)
    {
        std::vector<int> starts{ 0 };
        for (size_t i = 0; i < text.size(); ++i) if (text[i] == L'\n') starts.push_back(int(i) + 1);
        firstLine = std::max(0, firstLine); lastLine = std::min(int(starts.size()) - 1, lastLine);
        auto lineText = [&](int l) { int s = starts[size_t(l)]; int e = l + 1 < int(starts.size()) ? starts[size_t(l) + 1] - 1 : int(text.size()); return text.substr(size_t(s), size_t(e - s)); };
        auto blank = [](const std::wstring& s) { return s.find_first_not_of(L" \t\r") == std::wstring::npos; };
        bool allCommented = true;
        for (int l = firstLine; l <= lastLine; ++l)
        {
            std::wstring s = lineText(l);
            size_t a = s.find_first_not_of(L" \t");
            if (!blank(s) && (a == std::wstring::npos || s.compare(a, 2, L"//") != 0)) allCommented = false;
        }
        std::vector<Edit> edits;
        for (int l = lastLine; l >= firstLine; --l)
        {
            std::wstring s = lineText(l);
            if (blank(s)) continue;
            int indent = int(s.find_first_not_of(L" \t"));
            int at = starts[size_t(l)] + indent;
            if (allCommented) edits.push_back({ at, s.compare(size_t(indent), 3, L"// ") == 0 ? 3 : 2, L"" });
            else edits.push_back({ at, 0, L"// " });
        }
        return edits;
    }
}
