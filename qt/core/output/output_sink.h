#pragma once

#include <QString>

// Port of engine/.../output/OutputSink.kt: where finished m4a files end up. The desktop app writes to a folder
// (LocalFolderSink); a web deployment would hand files to the browser instead.
namespace xgs::output {

enum class PutStatus { Stored, AlreadyExists, Failed };

struct PutResult {
    PutStatus status;
    QString error;   // Failed only: the text of the failure (Windows' message)
};

class OutputSink {
public:
    virtual ~OutputSink() = default;

    /// Whether fileName is taken. Throws std::invalid_argument for a name the sink does not accept.
    virtual bool exists(const QString& fileName) = 0;

    /// Moves the finished file at source into the sink under fileName. AlreadyExists (and nothing moved) when overwrite is
    /// false and fileName is already taken. Throws std::invalid_argument for a name the sink does not accept.
    virtual PutResult put(const QString& fileName, const QString& source, bool overwrite) = 0;
};

}  // namespace xgs::output
