#include <QtTest>

#include "process/line_splitter.h"

using namespace Qt::StringLiterals;
using xgs::process::LineSplitter;

namespace {

// Feeds the chunks in order, then finish(); the lines delivered.
QStringList split(const QList<QByteArray>& chunks) {
    QStringList lines;
    LineSplitter splitter([&](const QString& line) { lines.append(line); });
    for (const QByteArray& chunk : chunks) splitter.feed(chunk);
    splitter.finish();
    return lines;
}

}  // namespace

class LineSplitterTest : public QObject {
    Q_OBJECT

private slots:
    void splitsOnAllThreeLineEndings() {
        QCOMPARE(split({"a\nb\r\nc\rd"}), QStringList({u"a"_s, u"b"_s, u"c"_s, u"d"_s}));
        // each byte on its own
        QList<QByteArray> bytes;
        for (char c : QByteArray("a\nb\r\nc\rd")) bytes.append(QByteArray(1, c));
        QCOMPARE(split(bytes), QStringList({u"a"_s, u"b"_s, u"c"_s, u"d"_s}));
        // \r\r\n is a lone \r and then \r\n; \n\r is \n and then \r
        QCOMPARE(split({"a\r\r\nb\n\rc"}), QStringList({u"a"_s, QString(), u"b"_s, QString(), u"c"_s}));
    }

    void aCarriageReturnAtAChunkEndFollowedByALineFeedIsOneEnding() {
        QCOMPARE(split({"a\r", "\nb"}), QStringList({u"a"_s, u"b"_s}));
        QCOMPARE(split({"a\r", "", "\nb\r", "\n"}), QStringList({u"a"_s, u"b"_s}));
        // a \r at the end of a chunk is delivered at once, before the next chunk is there
        QStringList lines;
        LineSplitter splitter([&](const QString& line) { lines.append(line); });
        splitter.feed("x\r");
        QCOMPARE(lines, QStringList({u"x"_s}));
        splitter.feed("\n");
        QCOMPARE(lines, QStringList({u"x"_s}));
        splitter.feed("\n");
        QCOMPARE(lines, QStringList({u"x"_s, QString()}));
        splitter.finish();
        QCOMPARE(lines, QStringList({u"x"_s, QString()}));
    }

    void aMultiByteCharacterSplitAcrossChunksIsDecodedWhole() {
        const QByteArray text = u"한글 😀 é"_s.toUtf8();
        for (qsizetype cut = 1; cut < text.size(); ++cut)
            QCOMPARE(split({text.first(cut), text.sliced(cut)}), QStringList({u"한글 😀 é"_s}));
        QList<QByteArray> bytes;
        for (char c : text) bytes.append(QByteArray(1, c));
        QCOMPARE(split(bytes), QStringList({u"한글 😀 é"_s}));
        // a truncated sequence before a line ending is one U+FFFD (observed), wherever the chunks end
        QCOMPARE(split({"\xE3", "\x81", "\nb"}), QStringList({u"�"_s, u"b"_s}));
    }

    void theLastLineWithoutAnEndingIsDeliveredByFinish() {
        QStringList lines;
        LineSplitter splitter([&](const QString& line) { lines.append(line); });
        splitter.feed("a\nlast");
        QCOMPARE(lines, QStringList({u"a"_s}));
        splitter.finish();
        QCOMPARE(lines, QStringList({u"a"_s, u"last"_s}));
        // invalid UTF-8 (observed answers of Kotlin's reader): a lone continuation byte, an encoded surrogate, a truncated
        // sequence at the end
        QCOMPARE(split({"\x80x\xED\xA0\x80y\xF0\x9F\x98"}), QStringList({u"�x�y�"_s}));
    }

    void anEmptyInputDeliversNothing() {
        QCOMPARE(split({}), QStringList());
        QCOMPARE(split({"", ""}), QStringList());
        QCOMPARE(split({"\n"}), QStringList({QString()}));
        QCOMPARE(split({"\r\n"}), QStringList({QString()}));
        QCOMPARE(split({"a\r\n"}), QStringList({u"a"_s}));
    }

    void aLineOfTenMegabytesArrivesWhole() {
        QByteArray big(10 * 1024 * 1024, 'x');
        big[5 * 1024 * 1024] = '\xEA';   // a Korean letter in the middle: EA B0 80 (U+AC00)
        big[5 * 1024 * 1024 + 1] = '\xB0';
        big[5 * 1024 * 1024 + 2] = '\x80';
        QList<QByteArray> chunks;
        for (qsizetype at = 0; at < big.size(); at += 65536) chunks.append(big.mid(at, 65536));
        chunks.append("\nend");
        QElapsedTimer timer;
        timer.start();
        const QStringList lines = split(chunks);
        QVERIFY2(timer.elapsed() < 5000, qPrintable(u"%1 ms"_s.arg(timer.elapsed())));
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).size(), big.size() - 2);
        QCOMPARE(lines.at(0).at(5 * 1024 * 1024), QChar(0xAC00));
        QCOMPARE(lines.at(1), u"end"_s);
    }
};

QTEST_GUILESS_MAIN(LineSplitterTest)
#include "tst_line_splitter.moc"
