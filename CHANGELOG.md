# Changelog

## 1.0.0 (2026-10-17)

First release.

- **The Project pane** (Project menu): the active map's usermaps folder as a folder tree, and the selected folder's
  items as tiles or a list. Radiance is not needed.
  - Search as you type, filter by file type, and see the folder path and item count.
  - Create folders and files. Rename (F2) and delete (Del) send items to the Recycle Bin.
  - Context menus offer Show in Explorer, Copy Path and Refresh. The folder follows outside changes.
- **The Code Editor pane:** double-click a GSC, CSC, GSH, zone, CSV, GDT, JSON or other text file to open it in a tab.
  - Syntax colouring for GSC / CSC / GSH and the CSV / zone family, using Radiance's colours, plus C-like and JSON
    colouring.
  - Find / replace (Ctrl+F / Ctrl+H, Replace All as one undo step), go to line (Ctrl+G), word wrap (Alt+Z), text size,
    toggling line comments (Ctrl+/), Ctrl+Space completion, and Tab / Shift+Tab indenting.
  - Saving (Ctrl+S) keeps the file's encoding (UTF-8, UTF-8 BOM, UTF-16, Latin-1) and its line endings.
  - A file changed on disk reloads by itself, or asks first when you have unsaved edits.
  - Unsaved tabs ask before closing, including when Radiant itself closes.
- **Dock Along Bottom / Top / Left / Right** (full width or full height) for both panes, from the Project menu.
