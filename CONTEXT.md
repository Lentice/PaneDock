# PaneDock Context Glossary

This glossary pins the interaction vocabulary shared across the design spec, the UI, and the tickets. When a term here has an _Avoid_ line, the listed words must not appear in spec text, UI strings, ticket titles or identifiers.

## Structural terms

**Group**:
A named, saved working context, and the primary abstraction of the product. Selecting a Group restores the complete right-hand side: layout template, split ratios, panes, tabs, per-tab location, view mode, sort order, and which pane and tab were active. A Group is not a folder shortcut and not a bookmark — it is the whole arrangement.
_Avoid_: workspace, session, profile, project, favourite

**pane**:
One of the 1–4 rectangular regions on the right side that hosts file views. A pane is a container; it holds one or more tabs. The number and arrangement of panes is fixed by the layout template.
_Avoid_: window, view, frame, split

**tab**:
One navigable location within a pane. A tab owns its location, view mode, sort order and navigation history. Only the visible pane's active tab is realized as a live Shell view; the rest exist as persisted data.
_Avoid_: page, document

**layout template**:
One of exactly five fixed pane arrangements: single, left/right, top/bottom, three-pane, four-pane grid. PaneDock does not support arbitrary recursive splitting, so there is no general "split" operation to name.
_Avoid_: layout mode, split configuration, arrangement

**sidebar**:
The persistent left-hand region listing Groups. It is always visible and is not part of any Group's state.
_Avoid_: navigation pane, tree, panel

**active pane**:
The pane that receives keyboard commands, clipboard operations and shortcuts. Exactly one pane is active at any time, and it is visually indicated.
_Avoid_: focused pane, current pane, selected pane

## Shell terms

**Shell view**:
The Windows-supplied folder view hosted inside a realized tab, obtained through `IExplorerBrowser`. Everything the Shell view draws — icons, thumbnails, columns, selection rendering — belongs to Windows, not to PaneDock.
_Avoid_: file list, listing, explorer control

**realized**:
A tab is realized when it holds a live `IExplorerBrowser` instance with an HWND. An unrealized tab exists only as persisted state. "Realize" is the verb for the transition; it happens on activation, never in bulk.
_Avoid_: loaded, opened, instantiated, live (as a verb)

**Shell location**:
The persisted identity of where a tab points: parsing name plus known-folder identity plus a fallback path. Not every location has an ordinary filesystem path, and a display name is never an identity.
_Avoid_: path (when the location may be virtual), folder, directory

**unresolvable location**:
A Shell location that cannot currently be resolved — a disconnected network drive, a removed USB volume, a deleted folder. It produces a recoverable error state in the tab and never causes the saved configuration to be discarded.
_Avoid_: missing path, broken path, invalid folder

## State terms

**session document**:
The versioned JSON file under `%LOCALAPPDATA%\PaneDock` holding all Groups and the window placement. Written by atomic replace with the previous version retained as backup.
_Avoid_: config, settings file, save file, state file

**best-effort state**:
State that PaneDock attempts to restore but does not guarantee: selected items, scroll position, column widths. Distinguished from **required state** — layout, locations, tabs, view mode, sort — which must restore exactly or the restore is a defect.
_Avoid_: optional state, nice-to-have state
