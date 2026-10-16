# 自建檔案列表：可回應的 tab／Group 切換與有界背景工作

## Problem Statement

使用者偶爾切換 tab 後，需等待很久才看到更新的內容，同時一般檔案的原生右鍵選單也可能延遲。
目前尚無此次卡頓的可歸因 trace，不能將兩種症狀斷言為同一根因。
目前 pane 由 IExplorerBrowser 提供 Shell view，PaneDock 無法完整掌控列表資料取得、
可見項目優先順序與更新呈現，因此難以獨立改善列表 UX。

使用者已決定自行管理列表，取捨依序為 UX、CPU／memory usage／power saving、架構與可維護性。
本次只處理列表；右鍵選單維持原生行為，只保留未來可替換的整合位置。

## Solution

由 PaneDock 管理 pane 內列表的資料與互動，將慢速列舉及補資料與 UI 操作分離。
列表分批出現並可操作，可見項目的圖示、縮圖與必要屬性優先補齊；快速切換時舊結果不污染新 tab。
慢資料夾不應阻擋使用者改目的地或操作其他 pane。背景工作、資料與快取具有明確容量限制，
閒置以事件驅動，不靠輪詢或持續重掃維持更新。

保留 Group、pane、tab、layout template、導覽歷史及 session document 的既有語意。
保留 Shell 檔案操作、拖放與原生選單整合；選單本身的載入、呈現及延遲改善不在本次範圍。

## User Stories

1. As a PaneDock user, I want tab activation to acknowledge my action immediately, so that I know which tab I am using.
2. As a PaneDock user, I want files to appear in batches, so that I can start working before enumeration finishes.
3. As a PaneDock user, I want another pane to remain usable while a folder is slow, so that unrelated work can continue.
4. As a PaneDock user, I want to switch away from a slow tab, so that I am not trapped waiting for its location.
5. As a PaneDock user, I want rapid navigation to show only the latest destination, so that obsolete results do not replace my current content.
6. As a PaneDock user, I want Group transitions to restore every pane's correct destination, so that my saved working context remains reliable.
7. As a PaneDock user, I want visible items to receive icons and thumbnails first, so that useful content appears before off-screen decoration.
8. As a PaneDock user, I want missing optional metadata to leave basic file interaction available, so that slow properties do not block ordinary work.
9. As a PaneDock user, I want selection and keyboard focus to stay on the intended item during updates, so that actions do not target a different file.
10. As a PaneDock user, I want stable scroll positioning during batch updates, so that the list does not jump while I work.
11. As a PaneDock user, I want expensive sorting to communicate its progress without continuously shuffling rows, so that I can follow the list.
12. As a PaneDock user, I want mouse and keyboard multi-selection, so that I can efficiently act on several items.
13. As a PaneDock user, I want type-to-select and familiar navigation keys, so that replacing Shell view does not disrupt routine interaction.
14. As a PaneDock user, I want inline rename to preserve Shell operation behavior, so that errors and conflicts remain understandable.
15. As a PaneDock user, I want copy, cut, paste and delete to retain native operation handling, so that progress and conflicts continue to work.
16. As a PaneDock user, I want drag and drop between panes and other applications, so that my existing workflows remain available.
17. As a PaneDock user, I want native menus to act on the current selection or folder background, so that replacing the list does not change their targets.
18. As a PaneDock user, I want disconnected locations to show a recoverable error, so that I can retry without losing saved state.
19. As a PaneDock user, I want local folders, network locations and removable volumes to remain navigable, so that my current locations remain useful.
20. As a PaneDock user, I want supported virtual Shell locations to retain their identity and navigation, so that they are not treated as ordinary filesystem paths.
21. As a PaneDock user, I want OneDrive placeholders to retain native semantics, so that browsing does not unnecessarily download file contents.
22. As a PaneDock user, I want relevant file changes to update the list, so that I can trust what is displayed.
23. As a PaneDock user, I want returning to a recently visited tab to reuse bounded cached data when appropriate, so that repeat navigation feels fast.
24. As a PaneDock user, I want refreshed results to correct stale cached content, so that cached presence is not mistaken for current existence.
25. As a PaneDock user, I want opening more inactive tabs to avoid retaining a full live list for each one, so that memory remains controlled.
26. As a PaneDock user, I want large folders to remain interactable, so that their size does not freeze the UI.
27. As a PaneDock user, I want background activity to stop when no work is needed, so that leaving PaneDock open conserves power.
28. As a PaneDock user, I want resizing and mixed-DPI monitor changes to preserve a usable list, so that content and hit targets remain aligned.
29. As a PaneDock user using assistive technology, I want accessible item names, selection and focus, so that the new list remains operable.
30. As a PaneDock user, I want shutdown during pending list work to finish safely, so that obsolete results cannot access destroyed UI.
31. As a PaneDock user, I want my saved arrangement and required tab state to survive the transition, so that the new list does not discard my configuration.

## Implementation Decisions

- The user-approved direction replaces the prohibition on managing the file list. C++20, native Win32, WRL ComPtr, Windows 10 22H2／Windows 11 x64 and English app UI remain in force.
- Priority is UX first, then CPU／memory／power, then architecture and maintainability. Improvements require observed interaction and resource measurements; neither a runtime rewrite nor a new dependency is implied.
- Pane owns pane-local list behavior. The coordinator retains cross-pane layout, active-pane selection, cross-pane tab drag, session-save scheduling and shutdown coordination. Stable pane identities and HWND reuse remain applicable.
- List state owns item identity, displayed order, selection, focus, scroll anchoring, loading/error state and navigation generation. Pure model logic remains independent of HWND, COM and Windows headers.
- Enumeration and enrichment return bounded batches of value data. Every result belongs to a specific pane binding and navigation request; a Group switch, tab replacement or shutdown invalidates stale results.
- UI rendering and interaction must not initiate slow enumeration or property queries. Background work has globally bounded workers, queues and retained results. Cancellation stops interest promptly; it does not claim to forcibly interrupt arbitrary OS or COM calls.
- A blocked operation must not permit an unlimited accumulation of abandoned work. Worker-capacity exhaustion, retry and shutdown behavior must be designed and tested before rollout. A worker pool alone is not proof of responsiveness under permanent hangs.
- Existing single-STA-only rules need a narrow explicit replacement for list background work. UI and UI-owned Shell objects remain STA-owned; workers must own their own resources and apartments, and may not use UI-owned COM pointers directly. Reentrancy protections still apply at pump points.
- List virtualization and data retention are separate concerns. Row data, strings, property keys, cached snapshots, images and queues each need limits and eviction behavior. Inactive tabs retain necessary state; complete lists are not retained indefinitely per tab.
- Visible items receive enrichment first, with limited prefetch. Missing decorative data must not prevent basic interaction. Expensive sort keys require a stable completion policy that preserves item identity and anchors.
- Use change notifications and coalesced updates rather than polling. Notification overflow or loss permits a bounded rescan; unsupported location notification behavior needs an explicit fallback without an idle polling loop.
- Persisted Shell location remains parsing name, known-folder identity and fallback path. Display names, transient row numbers, PIDLs and COM pointers are not persisted identity. No destructive session migration is authorized.
- File operations continue through Shell IDataObject／IFileOperation with native progress and conflict behavior. Read-only enumeration does not authorize replacing Shell mutation operations with direct filesystem writes.
- Local filesystem and virtual Shell namespace enumeration may need different implementations. The exact enumeration backend, UI control, worker count and cache limits remain engineering choices to validate; this spec does not claim an API prototype has established them.
- The menu integration accepts a snapshot of current folder/item identities and invocation position. List state does not own menu handles, verb indexes or menu COM lifetime. Pointer and keyboard requests share the same target semantics.
- Existing native menu behavior remains. Replacing Shell view requires only the necessary selection/identity bridge; no custom basic menu, filtering, new loading strategy, timeout or menu process isolation is introduced.
- During transition, every initialized IExplorerBrowser still requires Destroy before its parent HWND is destroyed. Removing it cannot bypass cleanup, native command routing, selection-based operations or drag/drop integration.
- Before implementation, align product rules and domain terminology that currently equate realized content with IExplorerBrowser. Existing ADR decisions about C++/WRL and stable pane identity remain; completed ticket history is preserved. This feature does not adopt general process isolation or a plugin framework.

## Testing Decisions

- User-confirmed primary seam: Pane's public operations and observable list state, extending existing PaneHost-based tests. The user confirmed this choice on 2026-10-16. Shell/network/OneDrive behavior additionally requires real integration validation.
- Exercise navigation, partial results, loading/error state, selection and anchors through that seam. Deliver controlled result batches and errors at the asynchronous result boundary, without faking IExplorerBrowser or asserting internal helper sequences.
- Reuse existing behavioral coverage patterns for stale navigation completion, Group transitions, tab mutations and shutdown coordination. Pure state computations continue to use core tests where appropriate; avoid duplicating every scenario at multiple seams.
- Verify external behavior: old results never alter the current destination; selection remains on the same identity after reorder; canceled work cannot update a destroyed or rebound pane; retry preserves saved location.
- A controlled delayed source verifies other pane actions and destination changes remain available. Separate capacity-exhaustion scenarios verify queue bounds and the stated degraded behavior. No arbitrary sleep-based timing test stands in for these contracts.
- Real filesystem integration verifies batch completion, errors, change notification recovery and large-directory behavior. Shell integration needs real desktop checks for virtual locations, OneDrive placeholders, native operations, native menu target correctness, drag/drop and apartment/lifetime behavior.
- Keyboard, accessibility, inline rename, resizing and mixed-DPI behavior require real control/desktop validation. Do not report a model test as evidence of native focus or screen-reader correctness, and do not silently reopen the rejected flaky Shell UI automation direction.
- Compare Release measurements for cold/warm local folders, slow/unreachable network locations, a 100,000-item directory and four visible panes. Record activation feedback, time to first operable results, full enumeration, CPU time, peak/steady memory, idle I/O and background wakeups. The directory size is a workload, not an invented pass threshold.
- Retain the existing idle target of average CPU below 0.1% over ten minutes and zero idle disk I/O. Define reproducible interaction and memory budgets before implementation acceptance; no numeric latency or cache budget has yet been measured or agreed.
- Native menu checks cover compatibility only. This spec cannot use faster tab navigation as evidence that extension-induced menu delay was fixed.

## Out of Scope

- Right-click menu redesign, custom basic menus, extension filtering/blocklists, menu acceleration, menu timeouts and menu process isolation.
- A replacement file-operation engine, search index, preview pane, terminal, custom plugin framework or unrelated features.
- Language/runtime changes, broad coordinator refactoring or general multi-process architecture.
- Destructive persistence changes or weakening preservation of unknown persisted fields.
- An always-on logging subsystem as an independent deliverable; narrowly scoped measurement hooks may support verification without ongoing idle writes.
- A claim that every underlying Shell/provider call can be forcibly canceled or that all Windows/extension delays will disappear.

## Further Notes

This is a feature-level specification spanning multiple implementation tickets, not a single half-day-to-two-day ticket.
The recommended sequence is rule/terminology alignment, a single-pane list and enumeration proof, Shell compatibility, four-pane interaction and resource validation, then production replacement.
Splitting work must preserve the final functionality contract; a local-folder-only proof is not a finished replacement for existing namespace support.

The user's reported lag is the motivation, not a proven diagnosis. Commander research supports staged work, deferred metadata and bounded caching; it does not establish that a particular backend is faster on this machine.
The earlier suggestion of a custom basic menu was explicitly excluded by the user and is not an unresolved requirement here.

The user approved the 21-ticket breakdown on 2026-10-16. Local tickets PD-222–PD-242 now implement this spec's delivery plan; the Ticket overview is the sole authority for their status and blocking edges. Creating tickets does not mark their implementation complete.
GitHub repository was verified as Lentice/PaneDock. Its label inventory currently has no ready-for-agent label or configured triage vocabulary.
Before GitHub publication with the required triage label, run /setup-matt-pocock-skills. The test seam and breakdown have already been confirmed; no second confirmation is needed.
