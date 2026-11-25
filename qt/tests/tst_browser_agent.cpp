#include <QtTest>

#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

#include "lyrics/google/browser_agent.h"
#include "text/kt_text.h"

// Port of the tests of sidecar-host/src/browser.rs, camelCase (`https_addresses_are_allowed` -> httpsAddressesAreAllowed). The
// agent no longer polls a server (Q3, decision 1): a command test hands the command to BrowserAgent::handle and looks at the
// answer it gives, where Rust looked at what the agent posted to the fake server. Not ported (Q3, decision 3): the two contract
// file tests and the two loop tests. The page answers either inside the call or later (the real one answers through the message
// loop): every command and idle test runs both ways. Three tests are added for what the C++ adds (see the end).

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::lyrics::google::allowedNavigation;
using xgs::lyrics::google::allowedUrl;
using xgs::lyrics::google::BrowserAgent;
using xgs::lyrics::google::BrowserAnswer;
using xgs::lyrics::google::BrowserCommand;
using xgs::lyrics::google::decodeScriptResult;
using xgs::lyrics::google::Page;
using xgs::lyrics::google::PageOutcome;
using Type = BrowserCommand::Type;

namespace {

/// A page that remembers what it was asked and answers from a script, at once or when the test says so (`later`).
class FakePage final : public Page {
public:
    QStringList calls;
    std::deque<PageOutcome> evaluations;
    std::optional<QString> loadError;
    bool later = false;
    bool twice = false;   // a page that breaks its contract: answers every call twice

    void load(const QString& url, std::function<void(PageOutcome)> done) override {
        calls.append(u"load "_s + url);
        answer(std::move(done), loadError ? PageOutcome{false, *loadError} : PageOutcome{true, QString()});
    }

    void evaluate(const QString& script, std::function<void(PageOutcome)> done) override {
        calls.append(u"evaluate "_s + script);
        PageOutcome outcome{true, u"null"_s};
        if (!evaluations.empty()) {
            outcome = evaluations.front();
            evaluations.pop_front();
        }
        answer(std::move(done), outcome);
    }

    void reset(std::function<void(PageOutcome)> done) override {
        calls.append(u"reset"_s);
        answer(std::move(done), PageOutcome{true, QString()});
    }

    void close() override { calls.append(u"close"_s); }

    /// Gives the answers held back so far, in order.
    void answerHeldBack() {
        std::vector<std::function<void()>> held;
        held.swap(m_held);
        for (auto& give : held) give();
    }

private:
    void answer(std::function<void(PageOutcome)> done, PageOutcome outcome) {
        if (later) {
            m_held.push_back([done, outcome] { done(outcome); });
            if (twice) m_held.push_back([done, outcome] { done(outcome); });
        } else {
            done(outcome);
            if (twice) done(outcome);
        }
    }

    std::vector<std::function<void()>> m_held;
};

bool sameAnswer(const BrowserAnswer& actual, const BrowserAnswer& expected) {
    return actual.id == expected.id && actual.ok == expected.ok && actual.value == expected.value && actual.error == expected.error;
}

QString describe(const BrowserAnswer& answer) {
    return u"{id %1, ok %2, value %3, error %4}"_s.arg(answer.id)
        .arg(answer.ok ? u"true"_s : u"false"_s, answer.value ? u"\""_s + *answer.value + u'"' : u"none"_s, answer.error);
}

BrowserAnswer okAnswer(quint64 id, std::optional<QString> value = std::nullopt) {
    return BrowserAnswer{id, true, std::move(value), QString()};
}

BrowserAnswer failedAnswer(quint64 id, const QString& error) {
    return BrowserAnswer{id, false, std::nullopt, error};
}

/// Runs one command on the agent: the answers it gave (exactly one is expected), after the page has answered.
std::vector<BrowserAnswer> run(BrowserAgent& agent, FakePage& page, const BrowserCommand& command) {
    std::vector<BrowserAnswer> answers;
    agent.handle(command, [&answers](BrowserAnswer answer) { answers.push_back(std::move(answer)); });
    if (page.later) {
        // Held back: nothing is answered before the page answers.
        if (!answers.empty()) return {failedAnswer(0, u"answered before the page did"_s)};
        page.answerHeldBack();
    }
    return answers;
}

}  // namespace

#define QCOMPARE_ANSWER(actual, expected)                                                                     \
    QVERIFY2(sameAnswer((actual), (expected)),                                                                \
             qPrintable(u"got "_s + describe(actual) + u", expected "_s + describe(expected)))

class BrowserAgentTest : public QObject {
    Q_OBJECT

    static void pageAnswers() {
        QTest::addColumn<bool>("later");
        QTest::newRow("at once") << false;
        QTest::newRow("later") << true;
    }

private slots:
    // ---- which addresses the browser may open ----

    void httpsAddressesAreAllowed() {
        QVERIFY(allowedUrl(u"https://www.google.com/search?q=a+b&hl=en"));
        QVERIFY(allowedUrl(u"https://consent.google.com"));
        QVERIFY(!allowedUrl(u"https://"));
        QVERIFY(!allowedUrl(u"https:///path"));
        QVERIFY(!allowedUrl(u"https://user@evil.example/"));
    }

    void plainHttpIsAllowedForTheLoopbackOnly() {
        QVERIFY(allowedUrl(u"http://127.0.0.1:8080/x"));
        QVERIFY(allowedUrl(u"http://127.0.0.1/"));
        QVERIFY(allowedUrl(u"http://localhost/"));
        QVERIFY(allowedUrl(u"http://localhost:3000"));
        QVERIFY(!allowedUrl(u"http://www.google.com/"));
        QVERIFY(!allowedUrl(u"http://127.0.0.1.evil.com/"));
        QVERIFY(!allowedUrl(u"http://127.0.0.1@evil.com/"));
    }

    void otherSchemesAreRefused() {
        for (const QString& url : {u"file:///c:/windows/win.ini"_s, u"javascript:alert(1)"_s, u"data:text/html,x"_s,
                                   u"ftp://example.test/"_s, u"about:blank"_s, QString(), u"google.com"_s}) {
            QVERIFY2(!allowedUrl(url), qPrintable(url));
        }
    }

    void aControlCharacterOrASpaceRefusesTheAddress() {
        for (const QString& url : {u"https://example.test/\n"_s, u"https://example.test/a b"_s, u"https://exa\tmple.test/"_s}) {
            QVERIFY2(!allowedUrl(url), qPrintable(url));
        }
    }

    void navigationMayAlsoGoToTheBlankPage() {
        QVERIFY(allowedNavigation(u"about:blank"));
        QVERIFY(allowedNavigation(u"https://www.google.com/search?q=a"));
        QVERIFY(!allowedNavigation(u"about:srcdoc"));
        QVERIFY(!allowedNavigation(u"file:///c:/x"));
    }

    // ---- what a script gave ----

    void aStringResultLosesItsJsonQuotesAndEscapes() {
        QCOMPARE(decodeScriptResult(u"\"a\\nb \\\"c\\\" 한글\""_s), std::optional<QString>(u"a\nb \"c\" 한글"_s));
    }

    void aNullResultIsNone() {
        QCOMPARE(decodeScriptResult(u"null"_s), std::optional<QString>());
    }

    void anotherJsonValueIsKeptAsText() {
        QCOMPARE(decodeScriptResult(u"42"_s), std::optional<QString>(u"42"_s));
        QCOMPARE(decodeScriptResult(u"{\"a\":1}"_s), std::optional<QString>(u"{\"a\":1}"_s));
    }

    void textThatIsNotJsonIsNone() {
        QCOMPARE(decodeScriptResult(u"not json"_s), std::optional<QString>());
        QCOMPARE(decodeScriptResult(QString()), std::optional<QString>());
    }

    // ---- the commands (Rust: the loop, with the answer posted to a fake server) ----

    void aLoadCommandOpensThePageAndIsAnsweredOk_data() { pageAnswers(); }
    void aLoadCommandOpensThePageAndIsAnsweredOk() {
        QFETCH(bool, later);
        FakePage page;
        page.later = later;
        BrowserAgent agent(page);

        const auto answers = run(agent, page, BrowserCommand{1, Type::Load, u"https://www.google.com/search?q=x"_s});

        QCOMPARE(page.calls, QStringList{u"load https://www.google.com/search?q=x"_s});
        QCOMPARE(answers.size(), std::size_t(1));
        QCOMPARE_ANSWER(answers[0], okAnswer(1));
    }

    void anEvaluateCommandAnswersTheDecodedValue_data() { pageAnswers(); }
    void anEvaluateCommandAnswersTheDecodedValue() {
        QFETCH(bool, later);
        FakePage page;
        page.later = later;
        page.evaluations.push_back(PageOutcome{true, u"\"two\\n한글\""_s});   // serde_json::to_string("two\n한글")
        BrowserAgent agent(page);

        const auto answers = run(agent, page, BrowserCommand{2, Type::Evaluate, u"document.title"_s});

        QCOMPARE(page.calls, QStringList{u"evaluate document.title"_s});
        QCOMPARE(answers.size(), std::size_t(1));
        QCOMPARE_ANSWER(answers[0], okAnswer(2, u"two\n한글"_s));
    }

    void aCommandThePageRefusesIsAnsweredWithItsErrorAndThePageIsClosed_data() { pageAnswers(); }
    void aCommandThePageRefusesIsAnsweredWithItsErrorAndThePageIsClosed() {
        QFETCH(bool, later);
        FakePage page;
        page.later = later;
        page.evaluations.push_back(PageOutcome{false, u"script timed out"_s});
        BrowserAgent agent(page);

        const auto answers = run(agent, page, BrowserCommand{3, Type::Evaluate, u"while(true){}"_s});

        QCOMPARE(answers.size(), std::size_t(1));
        QCOMPARE_ANSWER(answers[0], failedAnswer(3, u"script timed out"_s));
        QCOMPARE(page.calls, (QStringList{u"evaluate while(true){}"_s, u"close"_s}));
    }

    void aLoadOfAForbiddenAddressIsRefusedWithoutTouchingThePage_data() { pageAnswers(); }
    void aLoadOfAForbiddenAddressIsRefusedWithoutTouchingThePage() {
        QFETCH(bool, later);
        FakePage page;
        page.later = later;
        BrowserAgent agent(page);
        std::vector<BrowserAnswer> answers;

        // Refused without the page, so answered inside the call however the page answers.
        agent.handle(BrowserCommand{4, Type::Load, u"file:///c:/windows/win.ini"_s},
                     [&answers](BrowserAnswer answer) { answers.push_back(std::move(answer)); });

        QCOMPARE(answers.size(), std::size_t(1));
        QCOMPARE_ANSWER(answers[0], failedAnswer(4, u"url not allowed"_s));
        QVERIFY2(page.calls.isEmpty(), qPrintable(page.calls.join(u", ")));
    }

    void aCommandOfAnUnknownTypeIsAnsweredWithAnError_data() { pageAnswers(); }
    void aCommandOfAnUnknownTypeIsAnsweredWithAnError() {
        // Rust: a command whose `type` is not one it knows. Here the type is an enum: a value outside it.
        QFETCH(bool, later);
        FakePage page;
        page.later = later;
        BrowserAgent agent(page);
        std::vector<BrowserAnswer> answers;

        agent.handle(BrowserCommand{9, static_cast<Type>(42), u"x"_s},
                     [&answers](BrowserAnswer answer) { answers.push_back(std::move(answer)); });

        QCOMPARE(answers.size(), std::size_t(1));
        QCOMPARE_ANSWER(answers[0], failedAnswer(9, u"unknown command"_s));
        QVERIFY2(page.calls.isEmpty(), qPrintable(page.calls.join(u", ")));
    }

    // ---- an idle page ----

    void anIdlePageIsClosedAfterTheIdleTime_data() { pageAnswers(); }
    void anIdlePageIsClosedAfterTheIdleTime() {
        QFETCH(bool, later);
        FakePage page;
        page.later = later;
        BrowserAgent agent(page, 0ns);

        QCOMPARE(run(agent, page, BrowserCommand{1, Type::Load, u"https://www.google.com/"_s}).size(), std::size_t(1));
        agent.idleTick();

        QCOMPARE(page.calls, (QStringList{u"load https://www.google.com/"_s, u"close"_s}));
    }

    void aPageThatWasNeverOpenedIsNotClosedWhenIdle_data() { pageAnswers(); }
    void aPageThatWasNeverOpenedIsNotClosedWhenIdle() {
        QFETCH(bool, later);
        FakePage page;
        page.later = later;
        BrowserAgent agent(page, 0ns);

        agent.idleTick();
        agent.idleTick();

        QVERIFY2(page.calls.isEmpty(), qPrintable(page.calls.join(u", ")));
    }

    // ---- added: what the C++ adds ----

    void theControlAndWhitespaceSetIsRusts() {
        // Rust refuses a char that is_control (Cc) or is_whitespace (White_Space). That set is Kotlin's Char.isWhitespace together
        // with Cc (see isRustControlOrWhitespace in browser_agent.cpp): every BMP unit is checked against that.
        for (char32_t unit = 0; unit <= 0xFFFF; ++unit) {
            const QChar character(static_cast<char16_t>(unit));
            if (unit >= 0xD800 && unit <= 0xDFFF) continue;   // a lone surrogate: below
            const bool rustRefuses = xgs::kt::isWhitespace(character) || unit <= 0x1F || (unit >= 0x7F && unit <= 0x9F);
            QCOMPARE(allowedUrl(u"https://example.test/"_s + character), !rustRefuses);
        }
        QVERIFY(allowedUrl(u"https://example.test/\U0001F600"_s));   // a pair is one char, neither control nor white space
        QVERIFY2(!allowedUrl(u"https://example.test/"_s + QChar(char16_t(0xD800))), "a lone surrogate (no Rust string holds one)");
        QVERIFY(allowedUrl(u"http://127.0.0.1:/"_s) == false);   // an empty port is part of the host, as in Rust
        QVERIFY(!allowedUrl(u"HTTPS://example.test/"));          // the scheme is matched exactly, as strip_prefix does
    }

    void aScriptResultThatSerdeWouldRefuseIsNone() {
        // xgs::json reads Kotlin's grammar, which accepts more than serde_json; what it accepts beyond is nothing here.
        for (const QString& text : {u"abc"_s, u"NaN"_s, u"-Infinity"_s, u"01"_s, u"1."_s, u".5"_s, u"+1"_s, u"[1]x]"_s, u"[NaN]"_s,
                                    u"\"a\tb\""_s, u"\"\\ud800\""_s, u"{\"\\udc00\":1}"_s, u"{\"a\":1}x"_s}) {
            QVERIFY2(!decodeScriptResult(text).has_value(), qPrintable(text));
        }
        QCOMPARE(decodeScriptResult(u" [true, -0.5e+3, \"]x\"] "_s), std::optional<QString>(u"[true, -0.5e+3, \"]x\"]"_s));
        QCOMPARE(decodeScriptResult(u"\"\\ud83d\\ude00\""_s), std::optional<QString>(u"\U0001F600"_s));
        QCOMPARE(decodeScriptResult(u"false"_s), std::optional<QString>(u"false"_s));
    }

    void noIdlePageIsClosedWhileACommandIsUnderway() {
        // Rust handled a command before it looked at the idle time again; here the page may answer later, and the idle timer may
        // tick meanwhile. A page answering exactly once is the contract; a second answer of a broken page is ignored.
        FakePage page;
        BrowserAgent agent(page, 0ns);
        QCOMPARE(run(agent, page, BrowserCommand{1, Type::Load, u"https://www.google.com/"_s}).size(), std::size_t(1));
        page.later = true;
        std::vector<BrowserAnswer> answers;
        agent.handle(BrowserCommand{2, Type::Evaluate, u"1"_s}, [&answers](BrowserAnswer answer) { answers.push_back(answer); });

        agent.idleTick();
        QCOMPARE(page.calls, (QStringList{u"load https://www.google.com/"_s, u"evaluate 1"_s}));
        page.answerHeldBack();
        agent.idleTick();

        QCOMPARE(page.calls, (QStringList{u"load https://www.google.com/"_s, u"evaluate 1"_s, u"close"_s}));
        QCOMPARE(answers.size(), std::size_t(1));
        QCOMPARE_ANSWER(answers[0], okAnswer(2));

        page.twice = true;
        for (const bool later : {false, true}) {
            page.later = later;
            answers.clear();
            agent.handle(BrowserCommand{3, Type::Reset, QString()}, [&answers](BrowserAnswer answer) { answers.push_back(answer); });
            page.answerHeldBack();
            QCOMPARE(answers.size(), std::size_t(1));
        }
    }
};

QTEST_APPLESS_MAIN(BrowserAgentTest)
#include "tst_browser_agent.moc"
