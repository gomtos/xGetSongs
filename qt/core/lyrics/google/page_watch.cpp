#include "lyrics/google/page_watch.h"

#include <utility>
#include <variant>

#include "json/json.h"
#include "lyrics/google/page_classifier.h"

using namespace Qt::StringLiterals;

namespace xgs::lyrics::google {

namespace {

std::optional<QString> stringField(const json::Object& object, QStringView key) {
    const json::Value* value = json::find(object, key);
    if (!value) return std::nullopt;
    const json::Primitive* primitive = std::get_if<json::Primitive>(&value->data);
    if (!primitive || !primitive->isString) return std::nullopt;
    return primitive->content;
}

std::optional<bool> booleanField(const json::Object& object, QStringView key) {
    const json::Value* value = json::find(object, key);
    if (!value) return std::nullopt;
    const json::Primitive* primitive = std::get_if<json::Primitive>(&value->data);
    if (!primitive || primitive->isString) return std::nullopt;
    if (primitive->content == u"true") return true;
    if (primitive->content == u"false") return false;
    return std::nullopt;
}

}  // namespace

std::optional<PageProbe> probeFromJson(const QString& text) {
    const std::optional<json::Value> root = json::parse(text);
    if (!root) return std::nullopt;
    const json::Object* object = std::get_if<json::Object>(&root->data);
    if (!object) return std::nullopt;
    std::optional<QString> url = stringField(*object, u"url");
    std::optional<QString> readyState = stringField(*object, u"readyState");
    const std::optional<bool> hasCard = booleanField(*object, u"hasCard");
    std::optional<QString> bodyText = stringField(*object, u"bodyText");
    if (!url || !readyState || !hasCard || !bodyText) return std::nullopt;
    return PageProbe{std::move(*url), std::move(*readyState), *hasCard, std::move(*bodyText)};
}

PageWatch::PageWatch(std::chrono::nanoseconds settleTime, Clock clock) : m_settleTime(settleTime), m_clock(std::move(clock)) {}

std::chrono::nanoseconds PageWatch::now() const {
    if (m_clock) return m_clock();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

bool PageWatch::isDone(const PageProbe& probe) {
    if (probe.hasCard) return true;
    if (blockOf(probe.url, std::nullopt, probe.bodyText)) return true;
    if (probe.readyState != u"complete"_s) {
        m_completeSince.reset();
        return false;
    }
    if (probe.url != m_watchedUrl) {
        m_watchedUrl = probe.url;
        m_completeSince.reset();
    }
    if (!m_completeSince) m_completeSince = now();
    return now() - *m_completeSince >= m_settleTime;
}

}  // namespace xgs::lyrics::google
