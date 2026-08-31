#include "file_operations/file_operations.h"

#include <cassert>
#include <optional>

int main() {
    using panedock::file_operations::OperationKind;
    using panedock::file_operations::select_operation;
    assert(select_operation(std::nullopt) == OperationKind::copy);
    assert(select_operation(DROPEFFECT_COPY) == OperationKind::copy);
    assert(select_operation(DROPEFFECT_MOVE) == OperationKind::move);
    assert(select_operation(DROPEFFECT_LINK) == OperationKind::unsupported);
    assert(select_operation(DROPEFFECT_COPY | DROPEFFECT_MOVE) ==
           OperationKind::unsupported);
}
