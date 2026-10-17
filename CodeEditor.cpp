#include "CodeEditor.h"
#include "modtools_plugin.hpp"
#include "QtLite.h"
#include "ProjectFs.h"
#include "Highlighter.h"
#include "RadiantDock.h"
#include <cwctype>

namespace editor
{
    namespace
    {
        struct Page
        {
            std::wstring path;
            pfs::TextFile file;
            hl::Lang lang = hl::Lang::None;
            ModToolsWidget page = nullptr, edit = nullptr, name = nullptr, pathLabel = nullptr;
            ModToolsWidget diskBar = nullptr, findBar = nullptr, findEdit = nullptr, replaceEdit = nullptr, replaceButton = nullptr, replaceAllButton = nullptr;
            ModToolsWidget caseBox = nullptr, wrapBox = nullptr, message = nullptr, caret = nullptr, indent = nullptr, encoding = nullptr, language = nullptr;
            void* highlighter = nullptr;
            ql::Font font;
            int fontPt = 10;
            bool shownModified = false, deleted = false;
            std::string lastCaret;
        };
        std::vector<Page*> g_pages;
        ModToolsPane g_pane = nullptr;
        ModToolsWidget g_root = nullptr, g_tabs = nullptr, g_empty = nullptr;
        void* g_editFilter = nullptr;
        void* g_closeFilter = nullptr;
        void* g_mainWindow = nullptr;
        ULONGLONG g_nextCaret = 0, g_nextDisk = 0;

        const ModToolsUiApi* Ui() { return rp::host->ui; }

        std::string Html(const std::wstring& w)
        {
            std::string s = pfs::Utf8(w), out;
            for (char c : s) { if (c == '<') out += "&lt;"; else if (c == '>') out += "&gt;"; else if (c == '&') out += "&amp;"; else out += c; }
            return ql::HtmlAscii(out);
        }
        void Message(Page* p, const std::string& text) { if (p && p->message) Ui()->SetLabelText(p->message, ("<span style='color:#9a9a9a'>" + text + "</span>").c_str()); }
        std::string Name(const Page* p) { return pfs::Utf8(pfs::FileName(p->path)); }

        Page* Current()
        {
            if (!g_tabs || g_pages.empty() || !ql::A().QTabWidget_widget) return nullptr;
            int index = Ui()->TabCurrent(g_tabs);
            void* w = index >= 0 ? ql::A().QTabWidget_widget(g_tabs, index) : nullptr;
            for (Page* p : g_pages) if (p->page == w) return p;
            return nullptr;
        }
        int IndexOf(Page* p) { return g_tabs && ql::A().QTabWidget_indexOf ? ql::A().QTabWidget_indexOf(g_tabs, p->page) : -1; }

        void RefreshTitle(Page* p)
        {
            bool modified = ql::DocModified(p->edit);
            p->shownModified = modified;
            std::string label = "<b>" + Html(pfs::FileName(p->path)) + "</b>" + (modified ? " <span style='color:#E2C08D'>&#9679;</span>" : "");
            Ui()->SetLabelText(p->name, label.c_str());
            int index = IndexOf(p);
            if (index >= 0 && ql::A().QTabWidget_setTabText)
            {
                ql::Str t(Name(p) + (modified ? " *" : ""));
                ql::A().QTabWidget_setTabText(g_tabs, index, &t);
            }
        }

        void UpdateCaret(Page* p)
        {
            ql::Caret c = ql::CaretOf(p->edit);
            if (!c.ok) return;
            int selected = c.selEnd - c.selStart;
            std::string text = "Ln " + std::to_string(c.line + 1) + ", Col " + std::to_string(c.column + 1) + (selected > 0 ? "  (" + std::to_string(selected) + " selected)" : "");
            if (text != p->lastCaret) { p->lastCaret = text; Ui()->SetLabelText(p->caret, text.c_str()); }
            if (ql::DocModified(p->edit) != p->shownModified) RefreshTitle(p);
        }

        // The editor's look lives in its own style sheet: an application style sheet (Radiant's theme) would override a
        // plain setFont. The QFont is kept for measuring the tab width.
        void ApplyFont(Page* p)
        {
            std::string css = "QPlainTextEdit { font-family: Consolas, 'Courier New', monospace; font-size: " + std::to_string(p->fontPt) + "pt;"
                " background: #1e1e1e; color: #d4d4d4; selection-background-color: #264F78; selection-color: #ffffff; border: none; }";
            ql::SetStyleSheet(p->edit, css.c_str());
            if (!p->font.live) { if (!p->font.Make("Consolas", p->fontPt)) return; }
            else p->font.SetPointSize(p->fontPt);
            p->font.Apply(p->edit);
            if (ql::A().QPlainTextEdit_setTabStopWidth) ql::A().QPlainTextEdit_setTabStopWidth(p->edit, 4 * p->font.AverageCharWidth());
        }

        // ---- highlighting -----------------------------------------------------------------------------------------
        std::vector<hl::Span> g_spans;
        void HighlightBlock(void* h, void* user, const wchar_t* text, int length)
        {
            auto* p = static_cast<Page*>(user);
            int state = ql::PreviousBlockState(h);
            int out = hl::HighlightLine(p->lang, text, length, state < 0 ? 0 : state, g_spans);
            for (const hl::Span& s : g_spans) ql::SetFormat(h, s.start, s.length, s.color);
            ql::SetBlockState(h, out);
        }

        // ---- load / save ------------------------------------------------------------------------------------------
        void LoadFromDisk(Page* p)
        {
            std::wstring error;
            pfs::TextFile file;
            if (!pfs::LoadText(p->path, file, error)) { Message(p, "Could not open the file: " + pfs::Utf8(error)); return; }
            ql::Caret before = ql::CaretOf(p->edit);
            p->file = file;
            ql::SetPlainText(p->edit, file.text);
            int caret = std::min<int>(before.position, int(file.text.size()));
            ql::SetCaret(p->edit, caret, caret, false);
            ql::SetDocModified(p->edit, false);
            p->deleted = false;
            Ui()->SetVisible(p->diskBar, 0);
            std::wstring enc = pfs::EncodingLabel(file.encoding) + (file.crlf ? L"  CRLF" : L"  LF");
            Ui()->SetLabelText(p->encoding, pfs::Utf8(enc).c_str());
            Ui()->SetLabelText(p->indent, file.spaces ? "Spaces: 4" : "Tabs");
            Message(p, "Opened " + Html(pfs::FileName(p->path)));
            RefreshTitle(p);
            UpdateCaret(p);
        }

        bool Save(Page* p)
        {
            if (!p) return false;
            std::wstring error;
            if (!pfs::SaveText(p->path, p->file, ql::PlainText(p->edit), error)) { Message(p, "Could not save: " + Html(error)); return false; }
            ql::SetDocModified(p->edit, false);
            p->deleted = false;
            Ui()->SetVisible(p->diskBar, 0);
            SYSTEMTIME t; GetLocalTime(&t);
            char buf[32]; snprintf(buf, sizeof buf, "Saved %02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
            Message(p, buf);
            RefreshTitle(p);
            return true;
        }

        // ---- editing helpers --------------------------------------------------------------------------------------
        std::vector<int> LineStarts(const std::wstring& text)
        {
            std::vector<int> starts{ 0 };
            for (size_t i = 0; i < text.size(); ++i) if (text[i] == L'\n') starts.push_back(int(i) + 1);
            return starts;
        }
        int LineOf(const std::vector<int>& starts, int offset) { return int(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin()) - 1; }

        std::wstring FindText(Page* p) { return pfs::Wide(rp::EditText(p->findEdit)); }
        bool MatchCase(Page* p) { return Ui()->IsChecked(p->caseBox) != 0; }

        bool FindNext(Page* p, bool backward)
        {
            std::wstring term = FindText(p);
            if (term.empty()) return false;
            int flags = (backward ? 1 : 0) | (MatchCase(p) ? 2 : 0);
            if (ql::Find(p->edit, term, flags)) { Message(p, ""); return true; }
            int wrapAt = backward ? int(ql::PlainText(p->edit).size()) : 0;
            ql::SetCaret(p->edit, wrapAt, wrapAt, false);
            if (ql::Find(p->edit, term, flags)) { Message(p, backward ? "Search wrapped to the end" : "Search wrapped to the start"); return true; }
            Message(p, "No matches for '" + Html(term) + "'");
            return false;
        }

        bool SameText(const std::wstring& a, const std::wstring& b, bool matchCase)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) if (matchCase ? a[i] != b[i] : std::towlower(a[i]) != std::towlower(b[i])) return false;
            return true;
        }

        void ReplaceOne(Page* p)
        {
            std::wstring term = FindText(p), with = pfs::Wide(rp::EditText(p->replaceEdit));
            if (term.empty()) return;
            if (SameText(ql::SelectedText(p->edit), term, MatchCase(p)))
            {
                ql::Caret c = ql::CaretOf(p->edit);
                ql::ApplyEdits(p->edit, { { c.selStart, c.selEnd - c.selStart, with } });
            }
            FindNext(p, false);
        }

        void ReplaceAll(Page* p)
        {
            std::wstring term = FindText(p), with = pfs::Wide(rp::EditText(p->replaceEdit));
            if (term.empty()) return;
            std::wstring text = ql::PlainText(p->edit);
            std::vector<ql::TextEdit> edits;
            bool mc = MatchCase(p);
            for (size_t i = 0; i + term.size() <= text.size();)
            {
                if (SameText(text.substr(i, term.size()), term, mc)) { edits.push_back({ int(i), int(term.size()), with }); i += term.size(); }
                else ++i;
            }
            std::reverse(edits.begin(), edits.end());
            if (!edits.empty()) ql::ApplyEdits(p->edit, edits);
            Message(p, "Replaced " + std::to_string(edits.size()) + " occurrence" + (edits.size() == 1 ? "" : "s"));
        }

        void OpenFind(Page* p, bool replace)
        {
            Ui()->SetVisible(p->findBar, 1);
            Ui()->SetVisible(p->replaceEdit, replace);
            Ui()->SetVisible(p->replaceButton, replace);
            Ui()->SetVisible(p->replaceAllButton, replace);
            std::wstring sel = ql::SelectedText(p->edit);
            if (!sel.empty() && sel.find(L'\n') == std::wstring::npos) Ui()->SetEditText(p->findEdit, pfs::Utf8(sel).c_str());
            Ui()->SetFocus(p->findEdit);
        }
        void CloseFind(Page* p) { Ui()->SetVisible(p->findBar, 0); Ui()->SetFocus(p->edit); }

        void GoToLine(Page* p)
        {
            std::wstring text = ql::PlainText(p->edit);
            auto starts = LineStarts(text);
            int line = 0;
            ql::Caret c = ql::CaretOf(p->edit);
            if (!ql::AskInt(g_root, "Go to Line", "Line (1-" + std::to_string(starts.size()) + "):", c.line + 1, 1, int(starts.size()), line)) return;
            int offset = starts[size_t(line - 1)];
            ql::SetCaret(p->edit, offset, offset, true);
            Ui()->SetFocus(p->edit);
        }

        void ToggleComments(Page* p)
        {
            std::wstring text = ql::PlainText(p->edit);
            auto starts = LineStarts(text);
            ql::Caret c = ql::CaretOf(p->edit);
            int first = LineOf(starts, c.selStart), last = LineOf(starts, c.selEnd);
            std::vector<ql::TextEdit> edits;
            for (const hl::Edit& e : hl::ToggleLineComments(text, first, last)) edits.push_back({ e.offset, e.remove, e.insert });
            ql::ApplyEdits(p->edit, edits);
        }

        // Tab / Shift+Tab: indent or unindent the selected lines; a Tab without a multi-line selection inserts spaces
        // to the next stop when the file is indented with spaces.
        bool Indent(Page* p, bool outdent)
        {
            ql::Caret c = ql::CaretOf(p->edit);
            std::wstring text = ql::PlainText(p->edit);
            auto starts = LineStarts(text);
            int first = LineOf(starts, c.selStart), last = LineOf(starts, c.selEnd);
            if (!outdent && first == last)
            {
                if (!p->file.spaces) return false;   // Qt inserts the tab
                ql::InsertText(p->edit, std::wstring(size_t(4 - c.column % 4), L' '));
                return true;
            }
            if (last > first && c.selEnd == starts[size_t(last)]) --last;   // a selection ending at a line start leaves that line alone
            std::vector<ql::TextEdit> edits;
            for (int l = last; l >= first; --l)
            {
                int s = starts[size_t(l)];
                if (outdent)
                {
                    int n = 0;
                    if (s < int(text.size()) && text[size_t(s)] == L'\t') n = 1;
                    else while (n < 4 && s + n < int(text.size()) && text[size_t(s + n)] == L' ') ++n;
                    if (n > 0) edits.push_back({ s, n, L"" });
                }
                else edits.push_back({ s, 0, p->file.spaces ? L"    " : L"\t" });
            }
            ql::ApplyEdits(p->edit, edits);
            return true;
        }

        void Complete(Page* p)
        {
            ql::Caret c = ql::CaretOf(p->edit);
            std::wstring text = ql::PlainText(p->edit);
            int start = c.position;
            while (start > 0 && (iswalnum(text[size_t(start - 1)]) || text[size_t(start - 1)] == L'_' || text[size_t(start - 1)] == L':' || text[size_t(start - 1)] == L'#')) --start;
            std::wstring prefix = text.substr(size_t(start), size_t(c.position - start));
            auto words = hl::Completions(p->lang, text, prefix, 40);
            if (words.empty()) { Message(p, "No completions"); return; }
            ql::Menu m(p->edit);
            if (!m.Ok()) return;
            for (const std::wstring& w : words) m.Add(pfs::Utf8(w));
            int chosen = m.ExecAt(ql::CaretGlobalBottom(p->edit));
            if (chosen < 0) return;
            ql::ApplyEdits(p->edit, { { start, int(prefix.size()), words[size_t(chosen)] } });
        }

        void SetWrap(Page* p, bool on) { if (ql::A().QPlainTextEdit_setLineWrapMode) ql::A().QPlainTextEdit_setLineWrapMode(p->edit, on ? 1 : 0); }

        // ---- closing ----------------------------------------------------------------------------------------------
        void DestroyPage(Page* p)
        {
            if (p->highlighter) { ql::DestroyHighlighter(p->highlighter); p->highlighter = nullptr; }
            if (g_editFilter) { ql::Uninstall(p->edit, g_editFilter); ql::Uninstall(p->findEdit, g_editFilter); ql::Uninstall(p->replaceEdit, g_editFilter); }
            int index = IndexOf(p);
            if (index >= 0 && ql::A().QTabWidget_removeTab) ql::A().QTabWidget_removeTab(g_tabs, index);
            ql::A().QObject_deleteLater(p->page);
            p->font.Reset();
            g_pages.erase(std::remove(g_pages.begin(), g_pages.end(), p), g_pages.end());
            delete p;
            if (g_pages.empty() && g_empty) Ui()->SetVisible(g_empty, 1);
        }

        // Save / Don't Save / Cancel for one file. False = keep it open.
        bool ConfirmClose(Page* p, void* parent)
        {
            if (!ql::DocModified(p->edit)) return true;
            int r = ql::Question(parent, "Save changes?", Name(p) + " has unsaved changes. Save them before closing?",
                                 ql::BtnSave | ql::BtnDiscard | ql::BtnCancel, ql::BtnSave);
            if (r == ql::BtnSave) return Save(p);
            return r == ql::BtnDiscard;
        }

        void OnTabClose(void*, void** args)
        {
            int index = args && args[1] ? *static_cast<int*>(args[1]) : -1;
            void* w = index >= 0 && ql::A().QTabWidget_widget ? ql::A().QTabWidget_widget(g_tabs, index) : nullptr;
            for (Page* p : g_pages)
                if (p->page == w) { if (ConfirmClose(p, g_root)) DestroyPage(p); return; }
        }

        // ---- keys -------------------------------------------------------------------------------------------------
        enum { Key_S = 0x53, Key_F = 0x46, Key_H = 0x48, Key_G = 0x47, Key_W = 0x57, Key_Z = 0x5A, Key_Space = 0x20, Key_Equal = 0x3D, Key_Plus = 0x2B, Key_Minus = 0x2D, Key_Slash = 0x2F };
        Page* PageOfWidget(void* w) { for (Page* p : g_pages) if (p->edit == w || p->findEdit == w || p->replaceEdit == w) return p; return nullptr; }

        bool EditorKey(Page* p, void* watched, int key, int mods, bool act)
        {
            using namespace ql;
            bool ctrl = mods == CtrlMod, none = mods == 0, shift = mods == ShiftMod;
            bool inEdit = watched == p->edit;
            if (ctrl && key == Key_S) { if (act) Save(p); return true; }
            if (ctrl && key == Key_F) { if (act) OpenFind(p, false); return true; }
            if (ctrl && key == Key_H) { if (act) OpenFind(p, true); return true; }
            if (ctrl && key == Key_G) { if (act) GoToLine(p); return true; }
            if (ctrl && (key == Key_Equal || key == Key_Plus)) { if (act) { p->fontPt = std::min(24, p->fontPt + 1); ApplyFont(p); } return true; }
            if ((ctrl || mods == (CtrlMod | ShiftMod)) && key == Key_Plus) { if (act) { p->fontPt = std::min(24, p->fontPt + 1); ApplyFont(p); } return true; }
            if (ctrl && key == Key_Minus) { if (act) { p->fontPt = std::max(6, p->fontPt - 1); ApplyFont(p); } return true; }
            if (mods == AltMod && key == Key_Z) { if (act) Ui()->SetChecked(p->wrapBox, !Ui()->IsChecked(p->wrapBox)); return true; }
            if (key == Key_F3 && (none || shift)) { if (act) FindNext(p, shift); return true; }
            if (key == Key_Escape && none && Ui() && p->findBar) { if (act) CloseFind(p); return true; }
            if (inEdit)
            {
                if (ctrl && key == Key_Space) { if (act) Complete(p); return true; }
                if (ctrl && key == Key_Slash) { if (act) ToggleComments(p); return true; }
                if (key == Key_Tab && none) return act ? Indent(p, false) : true;
                if ((key == Key_Backtab || key == Key_Tab) && shift) { if (act) Indent(p, true); return true; }
            }
            else if ((key == Key_Return || key == Key_Enter) && (none || shift))
            {
                if (act) { if (watched == p->replaceEdit && none) ReplaceOne(p); else FindNext(p, shift); }
                return true;
            }
            return false;
        }

        bool EditFilter(void*, void* watched, int type, void* ev)
        {
            if (type != ql::EvShortcutOverride && type != ql::EvKeyPress) return false;
            Page* p = PageOfWidget(watched);
            if (!p) return false;
            int key = ql::KeyCode(ev), mods = ql::KeyMods(ev) & (ql::CtrlMod | ql::AltMod | ql::ShiftMod);
            if (type == ql::EvShortcutOverride)
            {
                if (EditorKey(p, watched, key, mods, false)) ql::AcceptEvent(ev);   // ours, not one of Radiant's shortcuts
                return false;
            }
            return EditorKey(p, watched, key, mods, true);
        }

        // Radiant's main window closing: offer to save edited files first (the host does not unload plugins at exit).
        bool CloseFilter(void*, void* watched, int type, void* ev)
        {
            if (type != ql::EvClose || watched != g_mainWindow || !AnyModified()) return false;
            if (ConfirmCloseAll(g_mainWindow)) return false;
            ql::IgnoreEvent(ev);   // Cancel: Radiant stays open
            return true;
        }

        // ---- button callbacks --------------------------------------------------------------------------------------
        void OnSave(void* u) { Save(static_cast<Page*>(u)); }
        void OnReload(void* u) { LoadFromDisk(static_cast<Page*>(u)); }
        void OnFind(void* u) { OpenFind(static_cast<Page*>(u), false); }
        void OnReplace(void* u) { OpenFind(static_cast<Page*>(u), true); }
        void OnGoTo(void* u) { GoToLine(static_cast<Page*>(u)); }
        void OnSmaller(void* u) { auto* p = static_cast<Page*>(u); p->fontPt = std::max(6, p->fontPt - 1); ApplyFont(p); }
        void OnLarger(void* u) { auto* p = static_cast<Page*>(u); p->fontPt = std::min(24, p->fontPt + 1); ApplyFont(p); }
        void OnKeepMine(void* u)
        {
            auto* p = static_cast<Page*>(u);
            Ui()->SetVisible(p->diskBar, 0);
            p->file.writeTime = pfs::WriteTime(p->path);
        }
        void OnFindNext(void* u) { FindNext(static_cast<Page*>(u), false); }
        void OnFindPrev(void* u) { FindNext(static_cast<Page*>(u), true); }
        void OnReplaceOne(void* u) { ReplaceOne(static_cast<Page*>(u)); }
        void OnReplaceAll(void* u) { ReplaceAll(static_cast<Page*>(u)); }
        void OnCloseFind(void* u) { CloseFind(static_cast<Page*>(u)); }
        void OnWrap(void* u, int on) { SetWrap(static_cast<Page*>(u), on != 0); }

        ModToolsWidget Button(const char* text, const char* tip, ModToolsCallback fn, Page* p, ModToolsWidget parent, ModToolsWidget box)
        {
            ModToolsWidget b = Ui()->PushButton(text, parent);
            if (tip) Ui()->SetToolTip(b, tip);
            rp::host->OnClicked(rp::self, b, fn, p);
            Ui()->BoxAddWidget(box, b, 0);
            return b;
        }

        Page* MakePage(const std::wstring& path)
        {
            const ModToolsUiApi* ui = Ui();
            auto* p = new Page();
            p->path = path;
            p->lang = hl::LanguageFor(path);
            p->page = ui->Widget(g_root);
            ModToolsWidget v = ui->VBox(p->page);
            ui->LayoutMargins(v, 0, 0, 0, 0);
            ui->LayoutSpacing(v, 0);

            // file bar: name, unsaved mark and path on one row, the commands on the row below
            ModToolsWidget info = ui->HBox();
            ui->LayoutMargins(info, 6, 3, 6, 0);
            ui->LayoutSpacing(info, 6);
            p->name = ui->Label("", p->page);
            ui->BoxAddWidget(info, p->name, 0);
            p->pathLabel = ui->Label(("<span style='color:#808080'>" + Html(path) + "</span>").c_str(), p->page);
            ui->SetToolTip(p->pathLabel, pfs::Utf8(path).c_str());
            ql::IgnoreWidth(p->pathLabel);
            ui->BoxAddWidget(info, p->pathLabel, 1);
            ui->BoxAddLayout(v, info, 0);
            ModToolsWidget bar = ui->HBox();
            ui->LayoutMargins(bar, 6, 3, 6, 3);
            ui->LayoutSpacing(bar, 3);
            Button("Save", "Save (Ctrl+S)", OnSave, p, p->page, bar);
            Button("Reload", "Reload from disk", OnReload, p, p->page, bar);
            Button("Find", "Find (Ctrl+F)", OnFind, p, p->page, bar);
            Button("Replace", "Find and replace (Ctrl+H)", OnReplace, p, p->page, bar);
            Button("Go to Line", "Go to line (Ctrl+G)", OnGoTo, p, p->page, bar);
            p->wrapBox = ui->CheckBox("Wrap", p->page);
            ui->SetToolTip(p->wrapBox, "Word wrap (Alt+Z)");
            rp::host->OnChanged(rp::self, p->wrapBox, OnWrap, p);
            ui->BoxAddWidget(bar, p->wrapBox, 0);
            Button("A\xE2\x88\x92", "Smaller text (Ctrl+-)", OnSmaller, p, p->page, bar);
            Button("A+", "Larger text (Ctrl+=)", OnLarger, p, p->page, bar);
            ui->BoxAddStretch(bar, 1);
            ui->BoxAddLayout(v, bar, 0);

            // changed on disk while edited here
            p->diskBar = ui->Widget(p->page);
            ql::SetStyleSheet(p->diskBar, "background:#3A3220;");
            ModToolsWidget db = ui->HBox();
            ui->LayoutMargins(db, 8, 3, 8, 3);
            ui->BoxAddWidget(db, ui->Label("This file changed on disk while it has unsaved changes here.", p->diskBar), 1);
            Button("Reload (lose my changes)", nullptr, OnReload, p, p->diskBar, db);
            Button("Keep mine", nullptr, OnKeepMine, p, p->diskBar, db);
            ui->SetLayout(p->diskBar, db);
            ui->SetVisible(p->diskBar, 0);
            ui->BoxAddWidget(v, p->diskBar, 0);

            // find / replace
            p->findBar = ui->Widget(p->page);
            ModToolsWidget fb = ui->HBox();
            ui->LayoutMargins(fb, 6, 3, 6, 3);
            ui->LayoutSpacing(fb, 3);
            p->findEdit = ui->LineEdit("", p->findBar);
            ui->SetPlaceholder(p->findEdit, "Find");
            ui->BoxAddWidget(fb, p->findEdit, 1);
            p->replaceEdit = ui->LineEdit("", p->findBar);
            ui->SetPlaceholder(p->replaceEdit, "Replace");
            ui->BoxAddWidget(fb, p->replaceEdit, 1);
            Button("Previous", "Find previous (Shift+F3, Shift+Enter)", OnFindPrev, p, p->findBar, fb);
            Button("Next", "Find next (F3, Enter)", OnFindNext, p, p->findBar, fb);
            p->replaceButton = Button("Replace", "Replace this match and find the next", OnReplaceOne, p, p->findBar, fb);
            p->replaceAllButton = Button("Replace All", "Replace every match (one undo step)", OnReplaceAll, p, p->findBar, fb);
            p->caseBox = ui->CheckBox("Match case", p->findBar);
            ui->BoxAddWidget(fb, p->caseBox, 0);
            Button("Close", "Close (Esc)", OnCloseFind, p, p->findBar, fb);
            ui->SetLayout(p->findBar, fb);
            ui->SetVisible(p->findBar, 0);
            ui->BoxAddWidget(v, p->findBar, 0);

            // the text
            p->edit = ui->PlainTextEdit(p->page);
            SetWrap(p, false);
            ApplyFont(p);
            ui->BoxAddWidget(v, p->edit, 1);

            // status bar: message | caret | indentation | encoding and line endings | language
            ModToolsWidget sb = ui->HBox();
            ui->LayoutMargins(sb, 8, 2, 8, 2);
            ui->LayoutSpacing(sb, 16);
            p->message = ui->Label("", p->page); ql::IgnoreWidth(p->message); ui->BoxAddWidget(sb, p->message, 1);
            p->caret = ui->Label("", p->page); ui->BoxAddWidget(sb, p->caret, 0);
            p->indent = ui->Label("", p->page); ui->BoxAddWidget(sb, p->indent, 0);
            p->encoding = ui->Label("", p->page); ui->BoxAddWidget(sb, p->encoding, 0);
            p->language = ui->Label(pfs::Utf8(pfs::LanguageName(path)).c_str(), p->page); ui->BoxAddWidget(sb, p->language, 0);
            ui->BoxAddLayout(v, sb, 0);

            if (g_editFilter) { ql::Install(p->edit, g_editFilter); ql::Install(p->findEdit, g_editFilter); ql::Install(p->replaceEdit, g_editFilter); }
            return p;
        }
    }

    bool Create()
    {
        const ModToolsUiApi* ui = Ui();
        if (!ui || !rp::HasUi(&ui->TabWidget)) return false;
        g_root = ui->Widget(nullptr);
        ModToolsWidget v = ui->VBox(g_root);
        ui->LayoutMargins(v, 0, 0, 0, 0);
        ui->LayoutSpacing(v, 0);
        g_tabs = ui->TabWidget(g_root);
        if (ql::A().QTabWidget_setTabsClosable) ql::A().QTabWidget_setTabsClosable(g_tabs, true);
        if (ql::A().QTabWidget_setMovable) ql::A().QTabWidget_setMovable(g_tabs, true);
        if (ql::A().QTabWidget_setDocumentMode) ql::A().QTabWidget_setDocumentMode(g_tabs, true);
        ql::Connect(g_tabs, ql::A().sig_QTabWidget_tabCloseRequested, ql::A().meta_QTabWidget, OnTabClose, nullptr);
        ui->BoxAddWidget(v, g_tabs, 1);
        g_empty = ui->Label("<span style='color:#808080'>Double-click a script, zone, CSV or other text file in the Project pane to edit it here.</span>", g_root);
        ui->BoxAddWidget(v, g_empty, 0);
        g_editFilter = ql::MakeFilter(EditFilter, nullptr);
        g_pane = rp::host->CreatePane(rp::self, "Code Editor", g_root, 760, 540);
        if (!g_pane) return false;
        rp::host->ShowPane(rp::self, g_pane, 0);   // shown when a file is opened
        if (ModToolsWidget menu = rp::host->AddMenu(rp::self, "Project"))
        {
            rp::host->AddMenuAction(rp::self, menu, rp::host->PaneToggleAction(rp::self, g_pane));
            rdock::AddEdgeItems(menu, g_root, "Code Editor");
        }
        if (rp::Has(&rp::host->MainWindow) && rp::host->MainWindow)
            if ((g_mainWindow = ql::WidgetForHwnd(rp::host->MainWindow(rp::self))) != nullptr)
                if ((g_closeFilter = ql::MakeFilter(CloseFilter, nullptr)) != nullptr) ql::Install(g_mainWindow, g_closeFilter);
        return true;
    }

    void Destroy()
    {
        for (Page* p : std::vector<Page*>(g_pages))
        {
            if (p->highlighter) { ql::DestroyHighlighter(p->highlighter); p->highlighter = nullptr; }
            p->font.Reset();
            delete p;
        }
        g_pages.clear();
        g_pane = nullptr; g_tabs = nullptr; g_root = nullptr; g_editFilter = nullptr; g_closeFilter = nullptr; g_mainWindow = nullptr;
    }

    void* Pane() { return g_pane; }

    bool Open(const std::wstring& rawPath)
    {
        if (!g_tabs) return false;
        std::wstring path = pfs::FullPath(rawPath);
        for (Page* p : g_pages)
            if (pfs::EqualsCi(p->path, path))
            {
                Ui()->TabSetCurrent(g_tabs, IndexOf(p));
                rp::host->ShowPane(rp::self, g_pane, 1);
                Ui()->SetFocus(p->edit);
                return true;
            }
        Page* p = MakePage(path);
        g_pages.push_back(p);
        int index = Ui()->TabAdd(g_tabs, p->page, Name(p).c_str());
        if (ql::A().QTabWidget_setTabToolTip) { ql::Str tip(pfs::Utf8(path)); ql::A().QTabWidget_setTabToolTip(g_tabs, index, &tip); }
        Ui()->SetVisible(g_empty, 0);
        LoadFromDisk(p);
        // colouring for scripts, zones and the like; very large files stay plain (Qt colours a document all at once)
        if (p->lang != hl::Lang::None && p->file.text.size() < 4u * 1024 * 1024)
            p->highlighter = ql::AttachHighlighter(ql::Document(p->edit), HighlightBlock, p);
        Ui()->TabSetCurrent(g_tabs, index);
        // the first time: a tab beside Radiant's camera (Radiance opens files beside the Scene view)
        if (!rp::ConfigGetBool("editor", "placed", false) && rp::Has(&rp::host->TabPaneWith) && rp::host->TabPaneWith)
        {
            int r = rp::host->TabPaneWith(rp::self, g_pane, "Camera");
            rp::ConfigSet("editor", "placed", "1");
            rp::Log("code editor first placement beside the Camera: %d", r);
        }
        rp::host->ShowPane(rp::self, g_pane, 1);
        Ui()->SetFocus(p->edit);
        return true;
    }

    void Tick()
    {
        if (g_pages.empty()) return;
        ULONGLONG now = GetTickCount64();
        if (now >= g_nextCaret)
        {
            g_nextCaret = now + 100;
            if (Page* p = Current()) UpdateCaret(p);
        }
        if (now >= g_nextDisk)
        {
            g_nextDisk = now + 1000;
            for (Page* p : std::vector<Page*>(g_pages))
            {
                uint64_t write = pfs::WriteTime(p->path);
                if (write == 0) { if (!p->deleted) { p->deleted = true; Message(p, "The file was deleted or renamed on disk."); } continue; }
                if (write == p->file.writeTime) continue;
                if (!ql::DocModified(p->edit)) LoadFromDisk(p);   // nothing to lose: follow the file
                else Ui()->SetVisible(p->diskBar, 1);
            }
        }
    }

    bool AnyModified() { for (Page* p : g_pages) if (ql::DocModified(p->edit)) return true; return false; }

    bool ConfirmCloseAll(void* parent)
    {
        for (Page* p : std::vector<Page*>(g_pages))
            if (!ConfirmClose(p, parent)) return false;
        return true;
    }

    void PathRenamed(const std::wstring& from, const std::wstring& to)
    {
        std::wstring f = pfs::FullPath(from), t = pfs::FullPath(to);
        for (Page* p : g_pages)
        {
            std::wstring old = p->path;
            if (pfs::EqualsCi(old, f)) p->path = t;                                                    // the file itself
            else if (old.size() > f.size() && pfs::EqualsCi(old.substr(0, f.size()), f) && old[f.size()] == L'\\') p->path = t + old.substr(f.size());   // inside a renamed folder
            else continue;
            p->file.writeTime = pfs::WriteTime(p->path);
            p->deleted = false;
            Ui()->SetLabelText(p->pathLabel, ("<span style='color:#808080'>" + Html(p->path) + "</span>").c_str());
            RefreshTitle(p);
        }
    }

    std::string Status()
    {
        Page* p = Current();
        return "tabs=" + std::to_string(g_pages.size()) + " current=" + (p ? pfs::Utf8(p->path) : std::string("-")) + " modified=" + (AnyModified() ? "1" : "0")
            + " highlighter=" + (p && p->highlighter ? "1" : "0");
    }

    std::string SelfTest()
    {
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring path = std::wstring(tmp) + L"ProjectBrowser_selftest.gsc";
        std::wstring error;
        pfs::TextFile seed; seed.crlf = true; seed.encoding = pfs::Encoding::Utf8Bom;
        if (!pfs::SaveText(path, seed, L"function main()\n{\n    self thread foo();\n}\n", error)) return "fail: seed " + pfs::Utf8(error);
        if (!Open(path)) return "fail: open";
        Page* p = Current();
        if (!p || !pfs::EqualsCi(p->path, pfs::FullPath(path))) return "fail: current page";
        if (!p->highlighter && ql::HighlightingAvailable()) return "fail: no highlighter";
        if (ql::PlainText(p->edit) != L"function main()\n{\n    self thread foo();\n}\n") return "fail: loaded text";
        ToggleComments(p);   // caret at 0: comments line 1
        if (ql::PlainText(p->edit).rfind(L"// function main()", 0) != 0) return "fail: comment toggle";
        if (!ql::DocModified(p->edit)) return "fail: modified flag";
        ToggleComments(p);
        Ui()->SetEditText(p->findEdit, "foo");
        Ui()->SetEditText(p->replaceEdit, "bar");
        ReplaceAll(p);
        if (ql::PlainText(p->edit).find(L"bar();") == std::wstring::npos) return "fail: replace all";
        if (!Save(p)) return "fail: save";
        std::string bytes;
        {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) return "fail: reopen";
            char buf[512]; DWORD n = 0; ReadFile(h, buf, sizeof buf, &n, nullptr); CloseHandle(h);
            bytes.assign(buf, n);
        }
        if (bytes.compare(0, 3, "\xEF\xBB\xBF") != 0) return "fail: BOM lost";
        if (bytes.find("bar();\r\n}\r\n") == std::string::npos) return "fail: CRLF or text on disk: " + bytes;
        // an outside change while unmodified reloads by itself
        Sleep(20);
        pfs::TextFile outside = p->file;
        if (!pfs::SaveText(path, outside, L"// changed outside\n", error)) return "fail: outside write";
        g_nextDisk = 0; Tick();
        if (ql::PlainText(p->edit) != L"// changed outside\n") return "fail: reload after outside change";
        ql::SetDocModified(p->edit, false);
        DestroyPage(p);
        DeleteFileW(path.c_str());
        return "ok (highlighter " + std::string(ql::HighlightingAvailable() ? "on" : "unavailable") + ")";
    }
}
