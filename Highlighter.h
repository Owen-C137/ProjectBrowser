// Highlighter: line tokenizers for the code editor, ported from Radiance's highlighting definitions
// (Editor\Highlighting\GSC.xshd and CSV.xshd, VS Code Dark+ colours) plus a C-like set standing in for AvaloniaEdit's
// built-in C++/C#/JavaScript/CSS (GDT files use it, as in Radiance) and JSON. No Qt: unit tested.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hl
{
    enum class Lang { None, Gsc, Csv, CLike, Json };
    Lang LanguageFor(const std::wstring& path);

    struct Span { int start, length; uint32_t color; };

    // Colours one line. stateIn / the return value carry multi-line comments between lines:
    // 0 none, 1 inside /* */, 2 inside /@ @/ (GSC doc comment).
    int HighlightLine(Lang lang, const wchar_t* text, int length, int stateIn, std::vector<Span>& out);

    // Words offered by completion: Radiance's GSC list (scripts only) plus identifiers of 3+ characters in the text that
    // start with `prefix` (ignoring case), sorted, at most `limit`.
    std::vector<std::wstring> Completions(Lang lang, const std::wstring& text, const std::wstring& prefix, size_t limit);

    // Ctrl+/: toggle "// " on lines [firstLine, lastLine] of `text` ('\n' separated). Returns the edits as
    // (offset, removeCount, insertText) in descending offset order, ready to apply one by one.
    struct Edit { int offset; int remove; std::wstring insert; };
    std::vector<Edit> ToggleLineComments(const std::wstring& text, int firstLine, int lastLine);
}
