#include "report.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace {
QString g_path;
}

void Report::setPath(const QString &path)
{
    g_path = path;
}

void Report::put(const QString &key, const QJsonObject &value)
{
    if (g_path.isEmpty())
        return;

    QJsonObject root;
    QFile in(g_path);
    if (in.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(in.readAll());
        if (doc.isObject())
            root = doc.object();
    }
    root.insert(key, value);

    QSaveFile out(g_path);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.commit();
    }
}
