# PaneDock Planning Requirements

Date: 2026-08-20
Source: brainstorm session with the user, plus two independent technical reviews (codex, opencode)

## Goal Intention

Build a lightweight Windows desktop file manager that combines Q-Dir's 1–4 pane browsing with switchable, saved working contexts. Only basic functionality is wanted; the user explicitly asked not to plan features that would go unused. It must be stable, feel smooth, and consume no extra CPU or disk while idle.

## Scope Note

The user's framing, verbatim in intent: Groups that can be switched, 1–4 split file-explorer panes, nothing else planned. Stability and smoothness over feature count. No background resource consumption during normal idle.

## Confirmed Requirements

### End-user outcomes

- A Group is a named working context; selecting one restores the whole right-hand side (layout, split ratios, tabs, per-tab location, view mode, sort, active pane and tab).
- Five fixed layout templates: single, left/right, top/bottom, three-pane, four-pane grid. No arbitrary recursive splitting.
- The pane experience is the native Explorer experience: native icons and thumbnails, native context menus including installed shell extensions, drag and drop between panes and with other applications, copy/move/delete/rename with the standard dialogs, multi-select, network drives, USB volumes, OneDrive placeholders.
- Per-tab address field with back, forward and parent navigation.
- State persists automatically; window position and size restore.
- An unavailable location shows a recoverable error in that tab without discarding the saved configuration.
- Zero measurable CPU and no disk I/O while idle.

### Maintainer outcomes

- Governance lives in Markdown in the repository, not in tool configuration or scratchpad handoffs.
- Ticket status has exactly one home, so it cannot diverge.
- Rejected directions are recorded with the evidence that rejected them and the condition that would justify reopening.
- Unmeasured release gates fail closed rather than being assumed to pass.

## Resolved Decisions

- **Name**: PaneDock. Chosen from candidates that converged on "pane + dock/deck" semantics.
- **Language**: C++20 with `Microsoft::WRL::ComPtr` on native Win32. Recorded in `docs/adr/0001-cpp-wrl-win32-over-rust-and-csharp.md` with every rejected alternative and its reason.
- **File view**: host `IExplorerBrowser`; never reimplement the file list.
- **Test seam**: one seam, `core`, which stays free of COM and `windows.h`. The COM layers are validated by a manual prototype protocol instead.
- **Memory strategy**: architectural, not language-based. Only the visible pane's active tab holds a live browser; thumbnail caps are explicit; extensions load lazily.
- **Tracker vocabulary**: `planned`, `ready`, `in_progress`, `blocked`, `done`, `deferred`, `superseded`. Status lives only in the Ticket 總覽 table of `docs/tickets.md`.
- **Document locations**: spec at `docs/design-spec.md`; tracker at `docs/tickets.md`; one ticket per file at `docs/tickets/PD-xxx-*.md`.
- **Ticket naming**: `PD-<3-digit>-<kebab-slug>.md`. Numbers are never reserved in advance.
- **No CI.** The decisive validations (four-pane Shell behavior, native context menus, cross-pane drag and drop, mixed DPI) cannot produce meaningful results on a CI runner. Recorded in `docs/tickets.md` §計畫決策紀錄.

## Remaining Uncertainties

- **Whether selection restoration is achievable at acceptable risk.** No public Shell API exists; undocumented `LVM_*` messages are the likely path. PD-002 decides, and cutting the requirement is a legitimate outcome.
- **Whether multi-instance `IExplorerBrowser` is stable enough to build on at all.** PD-001 is the Go/No-Go gate; a No-Go redirects to a hand-built list view over `IShellFolder`.
- **Every performance number.** All 12 rows of `docs/performance-baseline.md` are estimates. None has been observed.
- **Third-party shell extension behavior.** Cannot be bounded in advance; NFR-006 states the ceiling honestly rather than promising stability we cannot deliver.

## Brainstorm Coverage

Covered: the product problem and core abstraction, layout templates, per-Group persisted state, native Shell behavior requirements, language and framework selection with alternatives, module boundaries, test seam design, memory and idle-resource strategy, naming, the prototype gate, and the process machinery to mirror from NimbleRun.

Not covered, deliberately: visual design, settings UI, keyboard shortcut assignment specifics, installer and distribution, licensing.
