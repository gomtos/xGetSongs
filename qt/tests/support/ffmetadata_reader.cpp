#include "support/ffmetadata_reader.h"

namespace xgs::testsupport {

QMap<QString, QString> readFfmetadata(const QString& text) {
    QMap<QString, QString> values;
    qsizetype i = text.indexOf(u'\n') + 1;
    while (i < text.size()) {
        QString key;
        QString value;
        bool inValue = false;
        while (i < text.size()) {
            const QChar c = text.at(i++);
            if (c == u'\\' && i < text.size()) {
                (inValue ? value : key).append(text.at(i++));
            } else if (c == u'\n') {
                break;
            } else if (c == u'=' && !inValue) {
                inValue = true;
            } else {
                (inValue ? value : key).append(c);
            }
        }
        if (inValue) values.insert(key, value);
    }
    return values;
}

}  // namespace xgs::testsupport
