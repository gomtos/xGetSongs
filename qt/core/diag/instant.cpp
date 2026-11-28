#include "diag/instant.h"

#include <chrono>

namespace xgs::diag {

namespace {

constexpr qint64 SecondsPerDay = 86'400;
constexpr qint64 NanosPerSecond = 1'000'000'000;

qint64 floorDiv(qint64 a, qint64 b) {
    qint64 q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// Days since 1970-01-01 of a civil date (proleptic Gregorian). The algorithms are the public domain `days_from_civil` and
// `civil_from_days` of Howard Hinnant's date library paper; they work over the whole range of an Instant.
qint64 daysFromCivil(qint64 y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const qint64 era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146'097 + static_cast<qint64>(doe) - 719'468;
}

void civilFromDays(qint64 z, qint64& year, unsigned& month, unsigned& day) {
    z += 719'468;
    const qint64 era = (z >= 0 ? z : z - 146'096) / 146'097;
    const unsigned doe = static_cast<unsigned>(z - era * 146'097);
    const unsigned yoe = (doe - doe / 1'460 + doe / 36'524 - doe / 146'096) / 365;
    const qint64 y = static_cast<qint64>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = y + (month <= 2 ? 1 : 0);
}

bool isLeap(qint64 y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int daysInMonth(qint64 y, int m) {
    static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && isLeap(y) ? 29 : days[m - 1];
}

bool isAsciiDigit(QChar c) {
    return c.unicode() >= u'0' && c.unicode() <= u'9';
}

// `count` ASCII digits at `at`, or -1.
int digitsAt(const QString& text, qsizetype at, int count) {
    if (at + count > text.size()) return -1;
    int value = 0;
    for (int i = 0; i < count; ++i) {
        const QChar c = text[at + i];
        if (!isAsciiDigit(c)) return -1;
        value = value * 10 + (c.unicode() - u'0');
    }
    return value;
}

QString pad(qint64 value, int width) {
    return QString::number(value).rightJustified(width, u'0');
}

}  // namespace

Instant Instant::ofEpochSecond(qint64 second, int nano) {
    return Instant{second, nano};
}

Instant Instant::ofEpochMilli(qint64 milli) {
    return Instant{floorDiv(milli, 1'000), static_cast<int>((milli - floorDiv(milli, 1'000) * 1'000) * 1'000'000)};
}

Instant Instant::fromFileTime(quint64 hundredNanos) {
    constexpr qint64 EpochDifference = 116'444'736'000'000'000LL;   // 1601-01-01 to 1970-01-01 in 100 ns
    const qint64 sinceEpoch = static_cast<qint64>(hundredNanos) - EpochDifference;
    const qint64 second = floorDiv(sinceEpoch, 10'000'000);
    return Instant{second, static_cast<int>((sinceEpoch - second * 10'000'000) * 100)};
}

Instant Instant::now() {
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const qint64 second = floorDiv(nanos, NanosPerSecond);
    return Instant{second, static_cast<int>(nanos - second * NanosPerSecond)};
}

Instant Instant::min() {
    return Instant{-31'557'014'167'219'200LL, 0};
}

Instant Instant::max() {
    return Instant{31'556'889'864'403'199LL, 999'999'999};
}

std::optional<Instant> Instant::parse(const QString& text) {
    // yyyy-MM-ddTHH:mm:ss[.f{1,9}]Z
    if (text.size() < 20 || text[4] != u'-' || text[7] != u'-' || text[10] != u'T' || text[13] != u':' || text[16] != u':') {
        return std::nullopt;
    }
    const int year = digitsAt(text, 0, 4);
    const int month = digitsAt(text, 5, 2);
    const int day = digitsAt(text, 8, 2);
    const int hour = digitsAt(text, 11, 2);
    const int minute = digitsAt(text, 14, 2);
    const int second = digitsAt(text, 17, 2);
    if (year < 0 || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 59 || day > daysInMonth(year, month)) {
        return std::nullopt;
    }
    qsizetype at = 19;
    int nano = 0;
    if (text[at] == u'.') {
        ++at;
        int digits = 0;
        while (at < text.size() && isAsciiDigit(text[at])) {
            if (digits == 9) return std::nullopt;
            nano = nano * 10 + (text[at].unicode() - u'0');
            ++digits;
            ++at;
        }
        if (digits == 0) return std::nullopt;
        for (; digits < 9; ++digits) nano *= 10;
    }
    if (at != text.size() - 1 || text[at] != u'Z') return std::nullopt;
    const qint64 days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    return Instant{days * SecondsPerDay + hour * 3'600 + minute * 60 + second, nano};
}

QString Instant::toString() const {
    const qint64 days = floorDiv(epochSecond, SecondsPerDay);
    const qint64 secondOfDay = epochSecond - days * SecondsPerDay;
    qint64 year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(days, year, month, day);

    QString out;
    const qint64 absYear = year < 0 ? -year : year;
    if (year < 0) out += u'-';
    else if (year > 9'999) out += u'+';
    out += pad(absYear, 4);
    out += u'-' + pad(month, 2) + u'-' + pad(day, 2) + u'T' + pad(secondOfDay / 3'600, 2) + u':' + pad(secondOfDay % 3'600 / 60, 2) +
           u':' + pad(secondOfDay % 60, 2);
    if (nano != 0) {
        if (nano % 1'000'000 == 0) out += u'.' + pad(nano / 1'000'000, 3);
        else if (nano % 1'000 == 0) out += u'.' + pad(nano / 1'000, 6);
        else out += u'.' + pad(nano, 9);
    }
    out += u'Z';
    return out;
}

Instant Instant::plusSeconds(qint64 seconds) const {
    return Instant{epochSecond + seconds, nano};
}

Instant Instant::minusSeconds(qint64 seconds) const {
    return Instant{epochSecond - seconds, nano};
}

}  // namespace xgs::diag
