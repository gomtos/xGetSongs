#include "job_process.h"

#include <windows.h>

JobProcess::JobProcess(QObject *parent) : QObject(parent)
{
    // Unnamed job, default security: the handle is NOT inheritable, so no child keeps the job
    // alive after this process is gone (that would defeat KILL_ON_JOB_CLOSE).
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    m_job = job;

    m_process.setProcessChannelMode(QProcess::MergedChannels);
    // Drain the pipe so a chatty child (yt-dlp -J) never blocks on a full pipe.
    connect(&m_process, &QProcess::readyReadStandardOutput, this,
            [this] { m_process.readAllStandardOutput(); });
    connect(&m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus) { emit finished(exitCode); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit finished(-1);
    });
}

JobProcess::~JobProcess()
{
    if (m_job) {
        TerminateJobObject(static_cast<HANDLE>(m_job), 1);
        CloseHandle(static_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
}

bool JobProcess::start(const QString &program, const QStringList &arguments)
{
    if (!m_job || m_process.state() != QProcess::NotRunning)
        return false;

    m_processInfo = nullptr;
    m_process.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_SUSPENDED;
        // QProcess owns this PROCESS_INFORMATION until the process has finished; a suspended
        // child cannot finish, so it is still valid right after start() returns.
        m_processInfo = a->processInformation;
    });
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.start(); // CreateProcess runs synchronously inside start() on Windows

    auto *pi = static_cast<PROCESS_INFORMATION *>(m_processInfo);
    m_processInfo = nullptr;
    if (m_process.state() == QProcess::NotRunning || !pi || !pi->hProcess || !pi->hThread)
        return false;

    if (!AssignProcessToJobObject(static_cast<HANDLE>(m_job), pi->hProcess)) {
        TerminateProcess(pi->hProcess, 1); // never let it run outside the job
        return false;
    }
    if (ResumeThread(pi->hThread) == static_cast<DWORD>(-1)) {
        TerminateJobObject(static_cast<HANDLE>(m_job), 1);
        return false;
    }
    return true;
}

void JobProcess::cancel()
{
    if (m_job)
        TerminateJobObject(static_cast<HANDLE>(m_job), 1);
}

qint64 JobProcess::processId() const
{
    return m_process.processId();
}
