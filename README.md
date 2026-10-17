# Project and Code Editor (ProjectBrowser)

Radiance's Project dock and its code editor as plain Radiant dock panes. They work without Radiance: this is its own
MTPlugins plugin (`bin\plugins\ProjectBrowser\ProjectBrowser.dll`) and uses Radiant's Qt docking and the MTPlugins
host UI API.

Open the panes from **Project > Project** (it docks beside the Console the first time) and **Project > Code Editor**. The
Code Editor appears when you open a file, as a tab beside the Camera the first time.

## What was ported (1:1 with Radiance)

- **The project.** The active map's usermaps folder: `<...>\map_source\[sub\]<map>.map` maps to
  `<...>\usermaps\<map>`.
  - The folder tree shows "usermaps" with the map's folder (both open) and its directories, five levels deep.
    Junctions and links are skipped.
  - Opening a map selects the map's own folder.
- **Items.** The selected folder's subfolders, then its files, each sorted by name. Radiance's type labels are used
  (the extension in capitals, "File", "Folder").
- **Tiles or list.** The toolbar switches between large tiles and a list with a type column. The choice is
  remembered.
- **Search** in names, full paths and types, as you type.
- **Type filter.** The filter button lists "All types" and the file types present. Folders always stay visible so you
  can navigate. The header then says "· GSC only".
- **Header and footer.** The header shows the folder path ("usermaps › map › scripts") and "N items | usermaps/<map>",
  or "N of M items" while searching. The footer shows the selected item's full path.
- **Create folder / create file** in the selected folder.
  - Names are validated as in Radiance: one name, no path, no reserved characters.
  - Nothing can be created outside the project.
  - An existing name is refused with "'x' already exists in this folder."
- **Double-click.** A folder opens it. A text file (GSC, CSC, GSH, CSV, zone, GDT, JSON, or any file without NUL bytes,
  up to 16 MB) opens in the Code Editor.
- **The Code Editor.** One tab per file, with the file name, an unsaved marker and the path.
  - **Commands:** Save (Ctrl+S), Reload, Find (Ctrl+F), Replace (Ctrl+H), Go to Line (Ctrl+G), Wrap (Alt+Z), A− and A+
    (Ctrl+- and Ctrl+=).
  - **Status bar:** message, line and column with the selection length, Tabs or "Spaces: 4", the encoding and line
    ending, and the language.
  - **Syntax colouring** with Radiance's definitions and VS Code Dark+ colours:
    - GSC, CSC and GSH: comments and /@ @/ doc comments, strings, &"localized" and #"hash" strings, escapes, numbers,
      preprocessor lines, control words, keywords, self/level/game, constants, namespaces, ::function pointers and
      calls.
    - CSV, zone and zpkg: the first field, separators, quoted fields, numeric fields, #hashes, comments.
  - **Saving keeps the file's encoding** (UTF-8, UTF-8 with BOM, UTF-16 LE/BE, Latin-1 for old tool files) and its line
    endings.
  - **Changes on disk:** a file changed outside reloads by itself. If it has unsaved edits, a bar offers "Reload (lose
    my changes)" or "Keep mine".
  - **Closing a tab with unsaved changes** asks Save / Don't Save / Cancel.
  - **Ctrl+/** toggles `// ` on the selected lines.
  - **Ctrl+Space** offers completions: Radiance's GSC word list plus identifiers from the file.
  - Files indented with spaces keep using spaces when you press Tab.

## Additions (asked for in the brief, not in Radiance)

- **Rename** (F2) and **Delete** (Del) for files and folders, from the context menu or the keyboard.
  - Delete moves the item to the Recycle Bin, after a confirmation, so it can be restored.
  - The project root folders cannot be renamed or deleted.
  - Open editor tabs follow a renamed file or folder.
- **Context menus** on items, empty space and folders: Open, Rename, Delete, New Folder, New File, Show in Explorer,
  Copy Path and Refresh.
- **The folder view follows outside changes** (Explorer, compilers), checked every 2 seconds.
- **Find bar extras:** Previous / Next with wrap-around, Replace All as one undo step, Match case, F3 / Shift+F3,
  Enter in the find box.
- **Tab / Shift+Tab** indent and unindent the selected lines.
- **Unsaved edits when Radiant closes.** Closing Radiant (or disabling the plugin) with unsaved edits asks to save
  them. Cancel keeps Radiant open. This is needed because the host does not unload plugins when Radiant exits.

- **Dock along an outer edge.** The Project menu has "Dock Project / Code Editor Along Bottom (full width)" (and
  Top, Left, Right). Radiant's own drag and drop only places a pane beside or below *one* other pane; these entries
  use Radiant's layout call with no reference pane, which splits the whole dock area. Placing a pane under exactly
  two of three columns is not something Radiant's dock system supports. See `RadiantDock.h`. Verify after a Radiant
  update.

## What could not be matched 1:1, and why

Radiance's editor is AvaloniaEdit. In Radiant the editor is Qt's QPlainTextEdit, without Qt headers or a custom
subclass (no moc), so:

- **No line-number gutter and no current-line highlight.** Both need a QPlainTextEdit subclass that paints them. The
  status bar shows the line and column instead, and Go to Line jumps.
- **No code folding.** QPlainTextEdit has no folding.
- **Completion is a popup menu at the caret on Ctrl+Space.** AvaloniaEdit's completion window also opens by itself
  after three typed characters; a menu taking focus while you type would get in the way, so it does not auto-open.
- **No rectangular (Alt+drag) selection.** Qt's text edit does not support it.
- **Colouring covers GSC, CSC and GSH, the CSV family, a C-like set and JSON.** The C-like set covers what Radiance
  coloured with AvaloniaEdit's built-in C++/C#/JavaScript/CSS definitions (GDT, HLSL, C/C++, C#, JS, CSS); it is a
  simplified keyword, string, number and comment colouring, not the full definitions. XML, HTML, Python, Markdown,
  PowerShell and Lua open as plain text.
- **Files over 4 MB open without colouring.** Qt colours a whole document at once, while AvaloniaEdit colours lazily.
- **Mixed line endings are normalised to the file's main style when it is saved.** Qt's editor keeps one kind of line
  break. A file that is all CRLF or all LF is saved exactly as it was.
- **Toolbar icons are drawn line glyphs** in the shapes of Radiance's icons. Tiles show those glyphs rather than
  thumbnails (Radiance's Project tiles show glyphs too).
- **Labels are clipped rather than ending in "…"** when the pane is narrow. This keeps the panes dockable at any
  width.

## Building, testing, deploying

- Build: open `ProjectBrowser.vcxproj`, or run `MSBuild ProjectBrowser.vcxproj /p:Configuration=Release /p:Platform=x64`.
  The build does not deploy.
- Test: `Tests\RunTests.ps1` runs 65 checks of the usermaps mapping, listing, filters, create / rename / delete
  safety, encodings, line endings, colouring, completion and line comments.
- Deploy: `Deploy.ps1`. It refuses while Radiant, APE or the launcher is running. It backs up the deployed folder to
  `deployment-backups\<stamp>` and verifies the copied files by hash.
- Radiance (if it is enabled) replaces Radiant's window with its own editor. Switch to Radiant, or disable Radiance,
  to see these panes.
- Automation through MTPlugins' command channel (`OnCommand`): `status`, `folder <path>`, `open <file>`,
  `search <text>`, `type <TYPE>`, `tiles`, `list`, `new-folder <name>`, `new-file <name>`, `rename <path>|<name>`,
  `selftest` (edits, saves and reloads a scratch file in %TEMP%).

`QtLite.h` (the same file as in SceneHierarchy) holds the Qt calls the host API does not wrap. They are bound by
export name from Radiant's own Qt DLLs.
- The syntax highlighter is Qt's own QSyntaxHighlighter, with its one pure virtual pointed at this plugin. The vtable
  is checked against Qt's exported one before anything is patched; if the check fails, files open uncoloured.
- Everything that points into this DLL is torn down in `OnUnload`.
