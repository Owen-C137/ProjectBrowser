// CodeEditor: Radiance's code editor (Editor\Views\CodeEditorView) as a Radiant dock pane, one tab per open file:
// syntax colouring, find / replace, go to line, word wrap, text size, line comments, completion, and saving that keeps
// the file's encoding and line endings; files changed on disk reload, or ask when there are unsaved edits.
#pragma once
#include <string>

namespace editor
{
    bool Create();                                  // the pane (hidden until a file is opened)
    void Destroy();                                 // unload: drops the highlighters and pages (no prompts)
    bool Open(const std::wstring& path);            // opens (or switches to) a file and shows the pane
    void Tick();                                    // caret / modified / on-disk state, from OnTick
    bool AnyModified();
    bool ConfirmCloseAll(void* parent);             // Save / Discard / Cancel for unsaved files; false = cancel
    void* Pane();
    // A file was renamed or deleted in the Project pane: follow it (renamed) or mark the tab (deleted).
    void PathRenamed(const std::wstring& from, const std::wstring& to);
    std::string Status();                           // automation: "tabs=N current=<path> modified=0|1"
    std::string SelfTest();                         // automation: edits, saves and reloads a scratch file
}
