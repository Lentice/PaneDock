#include "file_operations/file_operations.h"
#include "unit/test_util.h"
#include <ole2.h>
#include <shlobj.h>
#include <string>

int main() {
    using namespace panedock::file_operations;
    EXPECT(select_operation(std::nullopt) == OperationKind::copy);
    EXPECT(select_operation(clipboard_effect(false)) == OperationKind::copy);
    EXPECT(select_operation(clipboard_effect(true)) == OperationKind::move);
    EXPECT(select_operation(DROPEFFECT_LINK) == OperationKind::unsupported);
    // Explorer's Ctrl+C publishes COPY|LINK; a cut publishes MOVE (possibly with more bits).
    EXPECT(select_operation(DROPEFFECT_COPY | DROPEFFECT_LINK) == OperationKind::copy);
    EXPECT(select_operation(DROPEFFECT_COPY | DROPEFFECT_MOVE) == OperationKind::move);
    EXPECT(select_operation(DROPEFFECT_MOVE | DROPEFFECT_LINK) == OperationKind::move);
    EXPECT(select_operation(DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK) == OperationKind::move);
    EXPECT(select_operation(DROPEFFECT_NONE) == OperationKind::unsupported);
    // Non-Shell clipboard content is offered to the folder's drop target.
    EXPECT(paste_drop_effects(std::nullopt) == DROPEFFECT_COPY);
    EXPECT(paste_drop_effects(DROPEFFECT_COPY | DROPEFFECT_LINK) == (DROPEFFECT_COPY | DROPEFFECT_LINK));
    EXPECT(paste_drop_effects(DROPEFFECT_MOVE | 0x80000000u) == DROPEFFECT_MOVE);
    {
        EXPECT(SUCCEEDED(OleInitialize(nullptr)));
        Microsoft::WRL::ComPtr<IDataObject> empty;
        EXPECT(SUCCEEDED(SHCreateDataObject(nullptr, 0, nullptr, nullptr, IID_PPV_ARGS(&empty))));
        if (empty) EXPECT(!offers_shell_items(*empty.Get())); // e.g. Outlook/browser descriptors
        wchar_t windows[MAX_PATH]{};
        EXPECT(GetWindowsDirectoryW(windows, MAX_PATH) > 0);
        const std::wstring items[]{windows};
        Microsoft::WRL::ComPtr<IDataObject> shell;
        EXPECT(SUCCEEDED(selection_data_object(items, Callbacks{}, shell)));
        if (shell) EXPECT(offers_shell_items(*shell.Get()));
        empty.Reset(); shell.Reset();
        OleUninitialize();
    }
    PasteResult result{S_OK, true, false, OperationKind::copy};
    EXPECT(completed_effect(result) == DROPEFFECT_COPY);
    result.operation = OperationKind::move;
    EXPECT(completed_effect(result) == DROPEFFECT_MOVE);
    result.aborted = true; // canceled or partially completed: never report full success
    EXPECT(!completed_effect(result));
    result.aborted = false;
    result.result = E_FAIL;
    EXPECT(!completed_effect(result));
    result.result = S_OK; result.handled = false;
    EXPECT(!completed_effect(result));
    result.handled = true; result.operation = OperationKind::unsupported;
    EXPECT(!completed_effect(result));
    return panedock::test::summary("file_operation_effect");
}
