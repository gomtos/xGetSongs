#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

#include "lyrics/lyrics_provider.h"
#include "model/models.h"

namespace xgs::testing {

/// Port of Kotlin's FakeLyricsProvider (engine/src/test/.../testutil/Fakes.kt): a LyricsProvider whose answer is a function;
/// records every query it is asked. Safe to call from several threads.
class FakeLyricsProvider final : public lyrics::LyricsProvider {
public:
    using Answer = std::function<std::optional<QString>(const LyricsQuery&)>;

    /// An empty answer function answers nothing (Kotlin's default `{ null }`).
    explicit FakeLyricsProvider(Answer answer = {}) : m_answer(std::move(answer)) {}

    std::optional<QString> find(const LyricsQuery& query, std::stop_token) override {
        {
            std::lock_guard lock(m_mutex);
            m_queries.append(query);
        }
        return m_answer ? m_answer(query) : std::nullopt;
    }

    QList<LyricsQuery> queries() const {
        std::lock_guard lock(m_mutex);
        return m_queries;
    }

private:
    Answer m_answer;
    mutable std::mutex m_mutex;
    QList<LyricsQuery> m_queries;
};

}  // namespace xgs::testing
