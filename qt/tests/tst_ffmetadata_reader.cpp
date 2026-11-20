#include <QtTest>

#include "support/ffmetadata_reader.h"
#include "tags/ffmetadata.h"

using namespace Qt::StringLiterals;
using xgs::tags::TrackTags;

// Port of FfmetadataReaderTest.kt.
class FfmetadataReaderTest : public QObject {
    Q_OBJECT

private slots:
    void readsBackWhatFfmetadataRenderWrote() {
        TrackTags tags;
        tags.title = u"a=b;c#d\\e"_s;
        tags.artist = u"방탄소년단"_s;
        tags.album = u"100% [x] {y} #1; k=v"_s;
        tags.albumArtist = u"Various Artists"_s;
        tags.trackNumber = 7;
        tags.comment = u"line one\nline two\r\nline three"_s;
        tags.lyrics = u"첫 줄\n\nLa la=la 🎵\n마지막"_s;

        const QMap<QString, QString> expected{
            {u"title"_s, tags.title},
            {u"artist"_s, tags.artist},
            {u"album_artist"_s, tags.albumArtist},
            {u"album"_s, *tags.album},
            {u"track"_s, u"7"_s},
            {u"comment"_s, *tags.comment},
            {u"lyrics"_s, *tags.lyrics},
        };
        QCOMPARE(xgs::testsupport::readFfmetadata(xgs::tags::renderFfmetadata(tags)), expected);
    }
};

QTEST_GUILESS_MAIN(FfmetadataReaderTest)
#include "tst_ffmetadata_reader.moc"
