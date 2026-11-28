#pragma once

#include <QString>
#include <QtGlobal>

#include <optional>

// java.time.Instant as far as the run marker needs it: a point in time with nanosecond precision, ordered, written and read as
// ISO-8601 in UTC (`2026-10-07T12:15:40.123Z`: the fraction in groups of three digits, left out when it is zero). The range is
// Java's, so that a hand-edited marker with an absurd time can be told apart from a real one.
namespace xgs::diag {

struct Instant {
    qint64 epochSecond = 0;
    int nano = 0;    // 0..999'999'999

    static Instant ofEpochSecond(qint64 second, int nano = 0);
    static Instant ofEpochMilli(qint64 milli);
    /// A FILETIME value (100 ns units since 1601-01-01 UTC).
    static Instant fromFileTime(quint64 hundredNanos);
    static Instant now();
    /// Java's Instant.MIN and Instant.MAX.
    static Instant min();
    static Instant max();

    /// The text of ISO-8601 `yyyy-MM-ddTHH:mm:ss[.fff[fff[fff]]]Z` with a four-digit year only; nullopt for anything else
    /// (also for the signed, longer years Instant.toString writes for the ends of the range, and for an impossible date).
    static std::optional<Instant> parse(const QString& text);

    /// Instant.toString(): years above 9999 get a `+`, negative ones a `-`.
    QString toString() const;

    Instant plusSeconds(qint64 seconds) const;
    Instant minusSeconds(qint64 seconds) const;

    friend auto operator<=>(const Instant&, const Instant&) = default;
};

}  // namespace xgs::diag
