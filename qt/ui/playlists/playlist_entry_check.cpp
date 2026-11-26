#include "playlist_entry_check.h"

#include "input/input_classifier.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::ui {

QString cleanName(const QString& raw, qsizetype maxUnits) {
    QString text;
    text.reserve(raw.size());
    for (const QChar unit : raw) {
        if (unit.category() != QChar::Other_Control) text.append(unit);
    }
    text = kt::trim(text);
    if (maxUnits >= 0 && text.size() > maxUnits) {
        qsizetype cut = maxUnits;
        if (cut > 0 && kt::isHighSurrogate(text.at(cut - 1))) --cut;   // do not leave half a pair
        text.truncate(cut);
        text = kt::trimEnd(text);
    }
    return text;
}

std::optional<QString> playlistIdOf(const QString& input) {
    const ClassifyResult result = classifyInput(input);
    if (!result.ok() || result.input->kind != ParsedInput::Kind::Playlist) return std::nullopt;
    return result.input->id;
}

EntryCheck checkEntry(const QString& name, const QString& idOrUrl, const QList<Playlist>& current, int editingIndex) {
    const QString cleaned = cleanName(name);
    if (cleaned.isEmpty()) return EntryCheck{std::nullopt, u"이름을 입력하세요"_s};

    const ClassifyResult classified = classifyInput(idOrUrl);
    if (!classified.ok()) {
        if (classified.reason == RejectReason::Empty) return EntryCheck{std::nullopt, u"재생목록 ID나 주소를 입력하세요"_s};
        if (classified.reason == RejectReason::MixPlaylist) return EntryCheck{std::nullopt, u"믹스 재생목록은 쓸 수 없습니다"_s};
        return EntryCheck{std::nullopt, u"재생목록 ID나 주소가 아닙니다"_s};
    }
    if (classified.input->kind != ParsedInput::Kind::Playlist) {
        return EntryCheck{std::nullopt, u"영상 하나의 주소입니다. 재생목록 ID나 주소를 넣으세요"_s};
    }
    const QString pid = classified.input->id;
    for (int i = 0; i < current.size(); ++i) {
        if (i != editingIndex && current.at(i).pid == pid) return EntryCheck{std::nullopt, u"이미 목록에 있습니다"_s};
    }
    return EntryCheck{Playlist{cleaned, pid}, QString()};
}

}  // namespace xgs::ui
