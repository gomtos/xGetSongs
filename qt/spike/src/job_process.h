#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

// Runs one child process inside a Win32 Job Object created with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
// The child is created suspended, assigned to the job, then resumed, so every process it ever
// starts is in the job too. cancel() ends the whole tree; if this process dies (even by
// TerminateProcess), the kernel closes the job handle and ends the tree as well.
class JobProcess : public QObject
{
    Q_OBJECT
public:
    explicit JobProcess(QObject *parent = nullptr);
    ~JobProcess() override;

    // Starts the child; it is in the job before it runs a single instruction.
    bool start(const QString &program, const QStringList &arguments);
    // TerminateJobObject: ends the child and all its descendants.
    void cancel();
    qint64 processId() const;

signals:
    void finished(int exitCode);

private:
    QProcess m_process;
    void *m_job = nullptr;            // HANDLE
    void *m_processInfo = nullptr;    // PROCESS_INFORMATION* owned by QProcess, seen in the modifier
};
