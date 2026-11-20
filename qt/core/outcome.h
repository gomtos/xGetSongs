#pragma once

#include <QString>

#include <optional>
#include <utility>

// The result of an operation that can fail or be cancelled, instead of an exception: a value when it succeeded, a message for
// the user when it failed, nothing when it was cancelled.
namespace xgs {

enum class OutcomeStatus { Ok, Failed, Cancelled };

template <class T>
struct Outcome {
    OutcomeStatus status = OutcomeStatus::Failed;
    std::optional<T> value;   // only when Ok
    QString error;            // only when Failed: the text shown to the user

    static Outcome ok(T v) {
        Outcome outcome;
        outcome.status = OutcomeStatus::Ok;
        outcome.value = std::move(v);
        return outcome;
    }

    static Outcome failed(QString message) {
        Outcome outcome;
        outcome.status = OutcomeStatus::Failed;
        outcome.error = std::move(message);
        return outcome;
    }

    static Outcome cancelled() {
        Outcome outcome;
        outcome.status = OutcomeStatus::Cancelled;
        return outcome;
    }

    bool isOk() const { return status == OutcomeStatus::Ok; }
};

}  // namespace xgs
