#pragma once

#include <QDialog>
#include <QString>

// The small 정보 dialog of design spec §6: the app and its version (CMake PROJECT_VERSION), the MIT licence, Qt used under the
// LGPLv3 with its version (qVersion()) and where its source is, the WebView2 SDK notice, and where the full texts are
// (THIRD-PARTY-NOTICES.md in the repository).
namespace xgs::app {

class AboutDialog final : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);

    /// Everything the dialog says, as plain text (for the tests).
    QString text() const;

    static QString appVersion();

private:
    QString m_text;
};

}  // namespace xgs::app
