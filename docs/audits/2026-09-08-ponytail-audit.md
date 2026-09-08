# Ponytail Audit — 2026-09-08

Scope: over-engineering and complexity only. Correctness, security, and performance are out of scope.

- `native` `ComRefCounted` hand-rolled IUnknown refcount duplicates WRL `RuntimeClass` already in use via `ComPtr` everywhere. Use `Microsoft::WRL::RuntimeClass` for the 3 subclasses. [src/com_ref_counted.h]
- `stdlib` `diagnostic_requested` hand-rolled case-insensitive compare loop duplicates `_wcsicmp`. Replace the lambda with `_wcsicmp`. [src/app_shell/diagnostic_mode.h:11-22]
- `shrink` Two `to_win32_rect` overloads split across `pane.h` and `main.cpp` do the same field copy on different rect types. Merge into one template in `window_helpers.h`. [src/app_shell/pane.h:34, src/app_shell/main.cpp:744]
- `shrink` 4-arg `compute_layout_rects` only forwards default minimums/thickness to the 7-arg overload. Replace with default args on one function. [src/core/layout.cpp:59-65]
- `delete` `navigation.h` exports one struct plus one inline used at a single prod site. Fold into `core/model.h`. [src/core/navigation.h]
- `delete` `window_placement.h` exports one `constexpr` used at a single call site. Fold into `core/layout.h` or `app_shell/window_helpers.h`. [src/app_shell/window_placement.h]

net: -120 lines, -2 headers possible.
