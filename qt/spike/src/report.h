#pragma once

#include <QJsonObject>
#include <QString>

// The report is one UTF-8 JSON object; every spike switch adds one top-level key.
namespace Report {

// Set once from --report; empty means "write nothing".
void setPath(const QString &path);

// Reads the report file (empty object if missing), sets `key`, rewrites it via QSaveFile.
// Does nothing when no report path was set.
void put(const QString &key, const QJsonObject &value);

} // namespace Report
