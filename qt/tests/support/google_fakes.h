#pragma once

#include <QString>
#include <QStringList>

#include <chrono>
#include <deque>
#include <functional>
#include <initializer_list>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

#include "lyrics/google/page_driver.h"
#include "lyrics/google/page_scripts.h"
#include "lyrics/google/rendering_browser.h"

// Port of the builders and fakes of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleTestSupport.kt, and of the
// FakeDriver of DrivenBrowserTest.kt. Kotlin built made-up HTML pages (testPage, testCard, testParagraph, testLine) and read
// them with Jsoup; here a page is what lyrics_card.js answers for it (Q3, decision 5). Every sentence is a dummy: there are no
// real lyrics in this repository.
namespace xgs::lyrics::google::testing {

inline const QString TestSearchUrl = QStringLiteral("https://www.google.com/search?q=x&hl=en");

/// A string as JavaScript's JSON.stringify writes it: `"` and `\` escaped, \b \f \n \r \t, other units below U+0020 as \u00xx
/// (lowercase hex), an unpaired surrogate as \uxxxx (lowercase hex), everything else as it is.
inline QString jsonStringifyString(const QString& text) {
    static constexpr char16_t hex[] = u"0123456789abcdef";
    auto escaped = [](char16_t unit) {
        QString out = QStringLiteral("\\u");
        for (int shift = 12; shift >= 0; shift -= 4) out += QChar(hex[(unit >> shift) & 0xF]);
        return out;
    };
    QString out = QStringLiteral("\"");
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t unit = text.at(i).unicode();
        const bool high = unit >= 0xD800 && unit <= 0xDBFF;
        const bool low = unit >= 0xDC00 && unit <= 0xDFFF;
        if (unit == u'"') out += QStringLiteral("\\\"");
        else if (unit == u'\\') out += QStringLiteral("\\\\");
        else if (unit == u'\b') out += QStringLiteral("\\b");
        else if (unit == u'\f') out += QStringLiteral("\\f");
        else if (unit == u'\n') out += QStringLiteral("\\n");
        else if (unit == u'\r') out += QStringLiteral("\\r");
        else if (unit == u'\t') out += QStringLiteral("\\t");
        else if (unit < 0x20) out += escaped(unit);
        else if (high && i + 1 < text.size() && text.at(i + 1).unicode() >= 0xDC00 && text.at(i + 1).unicode() <= 0xDFFF) {
            out += text.at(i);
            out += text.at(++i);
        } else if (high || low) out += escaped(unit);
        else out += text.at(i);
    }
    return out + u'"';
}

/// One line as lyrics_card.js reports it: its parent's number and its raw text.
struct ScriptLine {
    int parent;
    QString text;
};

/// The answer of lyrics_card.js: JSON.stringify({scope, lines: [{p, t}, ...]}).
inline QString scriptAnswer(bool scope, const std::vector<ScriptLine>& lines) {
    QString out = QStringLiteral("{\"scope\":") + (scope ? QStringLiteral("true") : QStringLiteral("false"))
                  + QStringLiteral(",\"lines\":[");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out += u',';
        out += QStringLiteral("{\"p\":") + QString::number(lines[i].parent) + QStringLiteral(",\"t\":")
               + jsonStringifyString(lines[i].text) + u'}';
    }
    return out + QStringLiteral("]}");
}

/// What lyrics_card.js answers for Kotlin's `testPage(testCard(testParagraph(...), testParagraph(...), ...))`: the card scope is
/// there, and every paragraph is one `div` holding its lines (joined by `<br>`, which lies between the line spans and not inside
/// them), so the lines of a paragraph share a parent and the paragraphs are numbered from 0.
inline QString cardResult(std::initializer_list<QStringList> paragraphs) {
    std::vector<ScriptLine> lines;
    int parent = 0;
    for (const QStringList& paragraph : paragraphs) {
        for (const QString& line : paragraph) lines.push_back(ScriptLine{parent, line});
        ++parent;
    }
    return scriptAnswer(true, lines);
}

/// A loaded page with a card of three dummy lines.
inline RenderResult loadedCard() {
    return RenderResult{RenderResult::Kind::Loaded, TestSearchUrl,
                        cardResult({{QStringLiteral("더미 하나"), QStringLiteral("더미 둘"), QStringLiteral("더미 셋")}}), QString(),
                        QString()};
}

/// A loaded result page with no lyrics card (Kotlin's `testPage("")`: no scope, no line).
inline RenderResult loadedWithoutCard() {
    return RenderResult{RenderResult::Kind::Loaded, TestSearchUrl, scriptAnswer(false, {}), QString(), QString()};
}

/// A loaded page that has a card marker but no lines in it (`<div data-lyricid="id-1"></div>`).
inline RenderResult loadedBrokenCard() {
    return RenderResult{RenderResult::Kind::Loaded, TestSearchUrl, scriptAnswer(true, {}), QString(), QString()};
}

/// Google's block page (`<html><body></body></html>`, "unusual traffic").
inline RenderResult loadedSorry() {
    return RenderResult{RenderResult::Kind::Loaded, QStringLiteral("https://www.google.com/sorry/index?continue=x"),
                        scriptAnswer(false, {}), QStringLiteral("unusual traffic"), QString()};
}

/// A browser that answers from a script and remembers the addresses it was asked for. Kotlin threw
/// (`error("unexpected request to $url")`) when the script had no answer left; render must not throw, so this one answers
/// Unavailable("unexpected request") and adds the address to `unexpected`, which a test checks.
class FakeBrowser final : public RenderingBrowser {
public:
    explicit FakeBrowser(std::vector<RenderResult> answers = {}) : m_queue(answers.begin(), answers.end()) {}

    QStringList urls;
    QStringList unexpected;

    RenderResult render(const QString& url, std::chrono::nanoseconds, std::stop_token) override {
        urls.append(url);
        if (m_queue.empty()) {
            unexpected.append(url);
            return RenderResult{RenderResult::Kind::Unavailable, QString(), QString(), QString(),
                                QStringLiteral("unexpected request")};
        }
        RenderResult answer = std::move(m_queue.front());
        m_queue.pop_front();
        return answer;
    }

private:
    std::deque<RenderResult> m_queue;
};

/// DrivenBrowserTest's FakeDriver: answers the probe from `probes` (the last one repeats) and the card read with `html` (Kotlin
/// read the page's HTML; the names stay). In Kotlin every look moved the test's time source by half a second, because the
/// time limit ran on the coroutine's virtual time and PageWatch on the time source; here one clock serves both and the test's
/// pause moves it, so a look does not.
class FakeDriver final : public PageDriver {
public:
    explicit FakeDriver(std::vector<std::optional<QString>> probes,
                        std::optional<QString> html = QStringLiteral("{\"scope\":false,\"lines\":[]}"))
        : m_probes(std::move(probes)), m_html(std::move(html)) {}

    QStringList calls;

    /// The answer of load when set.
    std::optional<DriverAnswer> loadFailure;

    /// The answer of the look at the page (the probe) when set.
    std::optional<DriverAnswer> probeFailure;

    /// The answer of the card read when set.
    std::optional<DriverAnswer> htmlFailure;

    /// The limit of every call, in order (load, probes, card read, reset), and whether reset's stop token could stop.
    std::vector<std::chrono::nanoseconds> limits;
    std::optional<bool> resetStopPossible;

    /// Called at the end of load (a test that runs render on another thread waits for it).
    std::function<void()> onLoad;

    DriverAnswer load(const QString& url, const Wait& wait) override {
        calls.append(QStringLiteral("load ") + url);
        limits.push_back(wait.limit);
        if (onLoad) onLoad();
        if (loadFailure) return *loadFailure;
        return DriverAnswer{DriverAnswer::Status::Ok, std::nullopt, QString()};
    }

    DriverAnswer evaluate(const QString& script, const Wait& wait) override {
        limits.push_back(wait.limit);
        if (script == cardScript()) {
            calls.append(QStringLiteral("html"));
            if (htmlFailure) return *htmlFailure;
            return DriverAnswer{DriverAnswer::Status::Ok, m_html, QString()};
        }
        calls.append(QStringLiteral("probe"));
        if (probeFailure) return *probeFailure;
        const std::size_t at = m_looks < m_probes.size() ? m_looks : m_probes.size() - 1;
        ++m_looks;
        return DriverAnswer{DriverAnswer::Status::Ok, m_probes.empty() ? std::nullopt : m_probes[at], QString()};
    }

    DriverAnswer reset(const Wait& wait) override {
        calls.append(QStringLiteral("reset"));
        limits.push_back(wait.limit);
        resetStopPossible = wait.stop.stop_possible();
        return DriverAnswer{DriverAnswer::Status::Ok, std::nullopt, QString()};
    }

    static DriverAnswer unavailable(const QString& reason) {
        return DriverAnswer{DriverAnswer::Status::Unavailable, std::nullopt, reason};
    }

    static DriverAnswer stopped() { return DriverAnswer{DriverAnswer::Status::Stopped, std::nullopt, QString()}; }

private:
    std::vector<std::optional<QString>> m_probes;
    std::optional<QString> m_html;
    std::size_t m_looks = 0;
};

}  // namespace xgs::lyrics::google::testing
