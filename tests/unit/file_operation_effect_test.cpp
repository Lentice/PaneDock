#include "file_operations/file_operations.h"
#include "unit/test_util.h"
#include <ole2.h>

int main() {
    using namespace panedock::file_operations;
    EXPECT(select_operation(std::nullopt) == OperationKind::copy);
    EXPECT(select_operation(clipboard_effect(false)) == OperationKind::copy);
    EXPECT(select_operation(clipboard_effect(true)) == OperationKind::move);
    EXPECT(select_operation(DROPEFFECT_LINK) == OperationKind::unsupported);
    EXPECT(select_operation(DROPEFFECT_COPY | DROPEFFECT_MOVE) == OperationKind::unsupported);
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
