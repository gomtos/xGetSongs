#pragma once

#include <QString>

// Port of engine/.../lyrics/google/GoogleLyricsResult.kt (the sealed interface GoogleLyricsResult and the interface
// GoogleLyricsLog).
namespace xgs::lyrics::google {

/// What one trip to Google's lyrics card came to. A lookup that goes wrong is a result, never an exception.
struct GoogleLyricsResult {
    enum class Kind {
        Found,               // the card was read; lyrics is tidy text: lines joined by `\n`, paragraphs by one blank line
        NoCard,              // the page loaded and has no lyrics card
        ExtractionFailed,    // the page has a lyrics card but its lines could not be read: Google probably changed its markup
        Captcha,             // Google's block page (CAPTCHA, "unusual traffic")
        Consent,             // Google's consent page
        RateLimited,         // HTTP 429 or a page that says "too many requests"
        Timeout,             // the page did not settle in time
        BrowserUnavailable,  // the browser cannot be used; reason is the name of what went wrong
        CoolingDown,         // Google is being left alone after trouble: no request was made
    };

    Kind kind = Kind::NoCard;
    QString lyrics;          // Found only
    int lineCount = 0;       // Found only
    int paragraphCount = 0;  // Found only
    QString reason;          // BrowserUnavailable only

    /// `Found(lines=N, paragraphs=M)`, `BrowserUnavailable(reason)`, otherwise the name of the kind. Lyrics are a
    /// copyrighted work: whatever prints a result (a log line, a failed test) must not print them, so this never does.
    QString toString() const;
};

/// Where the lookup says what it did. The text holds counts and names of results, never a song or lyrics.
class GoogleLyricsLog {
public:
    virtual ~GoogleLyricsLog() = default;
    virtual void info(const QString& message) = 0;
    virtual void warn(const QString& message) = 0;
};

}  // namespace xgs::lyrics::google
