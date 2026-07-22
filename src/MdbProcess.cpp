#include "MdbProcess.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <signal.h>
#endif

namespace {
QString quotedMdbPath(const QString& path)
{
    QString escaped = QDir::toNativeSeparators(path);
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}
}

MdbProcess::MdbProcess(QObject* parent)
    : QObject(parent)
{
    m_startupTimer.setSingleShot(true);
    connect(&m_startupTimer, &QTimer::timeout, this, &MdbProcess::onTimeout);
    connect(&m_process, &QProcess::started, this, &MdbProcess::onStarted);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &MdbProcess::onOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &MdbProcess::onOutput);
    connect(&m_process, &QProcess::errorOccurred, this, &MdbProcess::onError);
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MdbProcess::onFinished);
}

MdbProcess::~MdbProcess()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.write("quit\n");
        m_process.waitForFinished(500);
        terminateProcessTree(true);
        m_process.waitForFinished(1000);
    }
}

void MdbProcess::start(const HardwareDebugConfiguration& config)
{
    if (m_process.state() != QProcess::NotRunning)
        return;

    m_config = config;
    m_commands.clear();
    m_outputBuffer.clear();
    m_currentCommand.clear();
    m_waitingForPrompt = false;
    m_ready = false;
    m_stopping = false;
    m_lastSourceFile.clear();
    m_lastSourceLine = -1;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& key : config.serverEnvironment.keys())
        env.insert(key, config.serverEnvironment.value(key));
    m_process.setProcessEnvironment(env);
    if (!config.workingDirectory.isEmpty())
        m_process.setWorkingDirectory(config.workingDirectory);
    QString program = config.serverExecutable;
    QStringList arguments = config.serverArguments;
    m_usesProcessGroup = false;
    m_processGroupId = 0;
#ifdef Q_OS_UNIX
    const QString setsid = QStandardPaths::findExecutable(QStringLiteral("setsid"));
    if (!setsid.isEmpty()) {
        arguments.prepend(program);
        program = setsid;
        m_usesProcessGroup = true;
    }
#endif
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.start();
    // MPLAB may update probe firmware on the first connection. That operation
    // is expected to take much longer than a GDB-server startup, so this timer
    // represents inactivity rather than a hard deadline.
    m_startupTimer.start(qMax(config.startupTimeoutMs, 120000));
}

void MdbProcess::stop()
{
    m_stopping = true;
    m_startupTimer.stop();
    if (m_process.state() == QProcess::NotRunning)
        return;
    m_process.write("quit\n");
    if (!m_process.waitForFinished(m_config.shutdownTimeoutMs)) {
        terminateProcessTree(false);
        if (!m_process.waitForFinished(1000))
            terminateProcessTree(true);
    }
}

void MdbProcess::sendCommand(const QString& command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty())
        return;
    m_commands.enqueue(trimmed);
    if (!m_waitingForPrompt)
        dispatchNextCommand();
}

bool MdbProcess::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

void MdbProcess::onStarted()
{
    if (m_usesProcessGroup)
        m_processGroupId = m_process.processId();
    emit outputReceived(QStringLiteral("[MDB] Process started: %1\n").arg(m_config.serverExecutable));
    queueBootstrapCommands();
    // MDB prints an initial prompt before accepting the first command.
    m_waitingForPrompt = true;
}

void MdbProcess::queueBootstrapCommands()
{
    m_commands.enqueue(QStringLiteral("device %1").arg(m_config.mplabDevice));
    QString tool = QStringLiteral("hwtool %1").arg(m_config.mplabTool);
    if (!m_config.mplabToolSerialNumber.isEmpty())
        tool += QStringLiteral(" <sn>%1").arg(m_config.mplabToolSerialNumber);
    m_commands.enqueue(tool);
    for (const QString& command : m_config.preConnectCommands)
        m_commands.enqueue(command);
    for (const QString& command : m_config.postConnectCommands)
        m_commands.enqueue(command);
    for (const QString& command : m_config.preLoadCommands)
        m_commands.enqueue(command);
    if (m_config.loadImage && !m_config.programImage.isEmpty())
        m_commands.enqueue(QStringLiteral("program %1").arg(quotedMdbPath(m_config.programImage)));
    for (const QString& command : m_config.postLoadCommands)
        m_commands.enqueue(command);
    // A blank or production-programmed dsPIC may not have a usable debug
    // executive yet. MDB must program the debug image before reset can work.
    if (m_config.resetBeforeLoad)
        m_commands.enqueue(QStringLiteral("reset"));
    if (!m_config.initialBreakpoint.isEmpty())
        m_commands.enqueue(QStringLiteral("break %1").arg(m_config.initialBreakpoint));
    if (m_config.runAfterLoad)
        m_commands.enqueue(QStringLiteral("run"));
}

void MdbProcess::dispatchNextCommand()
{
    if (m_process.state() == QProcess::NotRunning || m_waitingForPrompt)
        return;
    if (m_commands.isEmpty()) {
        if (!m_ready) {
            m_ready = true;
            m_startupTimer.stop();
            emit ready();
        }
        return;
    }
    const QString command = m_commands.dequeue();
    m_currentCommand = command;
    emit outputReceived(QStringLiteral("[MDB] > %1\n").arg(command));
    m_process.write(command.toUtf8() + '\n');
    m_waitingForPrompt = true;
}

bool MdbProcess::outputHasPrompt() const
{
    static const QRegularExpression prompt(QStringLiteral("(?:^|[\\r\\n])\\s*>\\s*$"));
    return prompt.match(m_outputBuffer).hasMatch();
}

void MdbProcess::onOutput()
{
    const QString text = QString::fromLocal8Bit(m_process.readAllStandardOutput())
        + QString::fromLocal8Bit(m_process.readAllStandardError());
    if (text.isEmpty())
        return;
    if (!m_ready) {
        const bool firmwareUpdate = text.contains(
            QStringLiteral("Updating firmware"), Qt::CaseInsensitive);
        m_startupTimer.start(firmwareUpdate
            ? 600000
            : qMax(m_config.startupTimeoutMs, 120000));
    }
    m_outputBuffer += text;
    if (m_outputBuffer.size() > 8192)
        m_outputBuffer = m_outputBuffer.right(8192);
    emit outputReceived(text);
    if (text.contains(QRegularExpression(QStringLiteral("(?:^|[\\r\\n])Running(?:[\\r\\n]|$)"))))
        emit targetRunning();
    if (text.contains(QStringLiteral("Target Halted"), Qt::CaseInsensitive))
        emit targetStopped();

    static const QRegularExpression sourceLocationPattern(
        QStringLiteral("Stop at\\s+address:[^\\r\\n]+[\\r\\n]+\\s*file:([^\\r\\n]+)"
                       "[\\r\\n]+\\s*source line:(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto locationMatch = sourceLocationPattern.match(m_outputBuffer);
    if (locationMatch.hasMatch()) {
        const QString file = locationMatch.captured(1).trimmed();
        const int line = locationMatch.captured(2).toInt();
        if (file != m_lastSourceFile || line != m_lastSourceLine) {
            m_lastSourceFile = file;
            m_lastSourceLine = line;
            emit sourceLocation(file, line);
        }
    }
    if (m_waitingForPrompt && outputHasPrompt()) {
        const bool commandFailed = !m_currentCommand.isEmpty() &&
            (m_outputBuffer.contains(QStringLiteral("Program failed"), Qt::CaseInsensitive) ||
             m_outputBuffer.contains(QStringLiteral("in use by another MPLAB client"), Qt::CaseInsensitive) ||
             m_outputBuffer.contains(QStringLiteral("Fatal error"), Qt::CaseInsensitive) ||
             m_outputBuffer.contains(QStringLiteral("Failed to reset"), Qt::CaseInsensitive) ||
             m_outputBuffer.contains(QStringLiteral("Failed to connect"), Qt::CaseInsensitive) ||
             m_outputBuffer.contains(QStringLiteral("Connection failed"), Qt::CaseInsensitive));
        if (commandFailed) {
            const QString failedCommand = m_currentCommand;
            m_commands.clear();
            m_currentCommand.clear();
            m_waitingForPrompt = false;
            m_startupTimer.stop();
            const QString details = m_outputBuffer.trimmed().right(3000);
            emit errorOccurred(QStringLiteral("MDB command failed: %1\n%2")
                               .arg(failedCommand, details));
            QTimer::singleShot(0, this, &MdbProcess::stop);
            return;
        }
        m_waitingForPrompt = false;
        m_currentCommand.clear();
        m_outputBuffer.clear();
        dispatchNextCommand();
    }
}

void MdbProcess::terminateProcessTree(bool force)
{
#ifdef Q_OS_UNIX
    if (m_usesProcessGroup && m_processGroupId > 0) {
        ::kill(-static_cast<pid_t>(m_processGroupId), force ? SIGKILL : SIGTERM);
        return;
    }
#endif
    if (m_process.state() != QProcess::NotRunning) {
        if (force)
            m_process.kill();
        else
            m_process.terminate();
    }
}

void MdbProcess::onError(QProcess::ProcessError)
{
    if (!m_stopping)
        emit errorOccurred(QStringLiteral("MDB process error: %1").arg(m_process.errorString()));
}

void MdbProcess::onFinished(int exitCode, QProcess::ExitStatus status)
{
    m_startupTimer.stop();
    emit finished(exitCode, status);
    if (!m_stopping && !m_ready)
        emit errorOccurred(QStringLiteral("MDB exited before initialization completed (code %1).").arg(exitCode));
}

void MdbProcess::onTimeout()
{
    emit errorOccurred(QStringLiteral(
        "MDB produced no output while waiting for a command prompt. "
        "If probe firmware was being updated, reconnect the probe and let MPLAB X finish recovery."));
    stop();
}
