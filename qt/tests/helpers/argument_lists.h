#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <random>

// Random argument lists for the command-line tests: tst_system_process_runner (argumentsReachTheChildVerbatim) and the
// differential oracle's look at ProcessBuilder (`xgs-diff arglists`, Oracle.java argv). The same seed gives the same lists.
// An argument mixes spaces, tabs, `"`, `\`, `%`, `&`, `^`, `!`, `(`, `)`, Korean and emoji; some are empty and some end in
// one to three backslashes (with or without a space before them, so that they end up before a closing quote).
namespace xgs::testing {

inline QList<QStringList> argumentLists(int count, unsigned seed) {
    static const char16_t* const pieces[] = {
        u"a", u"b", u"xyz", u"0", u" ", u"  ", u"\t", u"\"", u"\"\"", u"\\", u"\\\\", u"\\\"", u"\\\\\"", u"%", u"%PATH%",
        u"&", u"&&", u"^", u"!", u"!x!", u"(", u")", u"|", u"<", u">", u"한글", u"가수 - 제목", u"😀", u"🎵", u"a b", u"C:\\Program Files\\",
        u"'", u"=", u",", u";", u"*", u"?", u"--opt=\"x y\"", u"é"};
    std::mt19937 random(seed);
    const auto below = [&](int n) { return int(random() % unsigned(n)); };
    QList<QStringList> lists;
    lists.reserve(count);
    for (int i = 0; i < count; ++i) {
        QStringList list;
        const int arguments = 1 + below(6);
        for (int k = 0; k < arguments; ++k) {
            QString argument;
            if (below(100) >= 12) {
                const int length = below(9);
                for (int p = 0; p < length; ++p) argument += QString::fromUtf16(pieces[below(int(std::size(pieces)))]);
                if (below(100) < 30) {
                    if (below(2) == 0) argument += u' ';
                    argument += QString(1 + below(3), u'\\');
                }
            }
            list.append(argument);
        }
        lists.append(list);
    }
    return lists;
}

}  // namespace xgs::testing
