#include "about_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace xgs::app {

namespace {

QLabel* paragraph(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    return label;
}

}  // namespace

QString AboutDialog::appVersion() {
    return QString::fromLatin1(XGS_APP_VERSION);
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(u"aboutDialog"_s);
    setWindowTitle(u"정보"_s);
    auto* layout = new QVBoxLayout(this);

    const QString title = u"xGetSongs "_s + appVersion();
    auto* heading = paragraph(title, this);
    heading->setObjectName(u"aboutTitle"_s);
    layout->addWidget(heading);

    const QStringList paragraphs = {
        u"YouTube 재생목록이나 영상의 오디오를 m4a로 받는 개인용 도구입니다. MIT 라이선스로 배포됩니다. Copyright (c) 2026 gomtos"_s,
        u"Qt "_s + QString::fromLatin1(qVersion()) +
            u"을 GNU LGPL v3에 따라 정적으로 링크해 씁니다. Qt 소스는 https://download.qt.io/official_releases/qt/ 에서 받을 수 있고, "_s
            u"이 앱의 소스와 빌드 방법이 저장소에 있어 다른 Qt로 다시 빌드할 수 있습니다."_s,
        u"Microsoft Edge WebView2 SDK를 씁니다. Copyright (C) Microsoft Corporation. BSD 3-Clause 형식의 라이선스입니다."_s,
        u"이 고지들과 LGPLv3·GPLv3 전문은 저장소의 THIRD-PARTY-NOTICES.md 파일에 있습니다."_s,
    };
    m_text = title;
    for (const QString& text : paragraphs) {
        layout->addWidget(paragraph(text, this));
        m_text += u'\n' + text;
    }

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* close = buttons->addButton(u"닫기"_s, QDialogButtonBox::AcceptRole);
    close->setObjectName(u"close"_s);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
    setMinimumWidth(460);
}

QString AboutDialog::text() const {
    return m_text;
}

}  // namespace xgs::app
