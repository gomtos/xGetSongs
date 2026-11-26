#include "ui_types.h"

namespace xgs::ui {

QList<int> failedRanks(const UiState& state) {
    QList<int> ranks;
    for (const ItemRow& row : state.rows) {
        if (row.status.kind == ItemStatus::Kind::Failed) ranks.append(row.item.rank);
    }
    return ranks;
}

bool canResolve(const UiState& state) {
    return state.phase != Phase::Resolving && state.phase != Phase::Running;
}

}  // namespace xgs::ui
