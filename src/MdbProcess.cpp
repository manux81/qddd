#include "MdbProcess.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>

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
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
}

void MdbProcess::start(const HardwareDebugConfiguration& config)
{
    if (m_process.state() != QProcess::NotRunning)
        return;

    m_config = config;
    m_commands.clear();
    m_outputBuffer.clear();
    m_waitingForPrompt = false;
    m_ready = false;
    m_stopping = false;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& key : config.serverEnvironment.keys())
        env.insert(key, config.serverEnvironment.value(key));
    m_process.setProcessEnvironment(env);
    if (!config.workingDirectory.isEmpty())
        m_process.setWorkingDirectory(config.workingDirectory);
    m_process.setProgram(config.serverExecutable);
    m_process.setArguments(config.serverArguments);
    m_process.start();
    m_startupTimer.start(config.startupTimeoutMs);
}

void MdbProcess::stop()
{
    m_stopping = true;
    m_startupTimer.stop();
    if (m_process.state() == QProcess::NotRunning)
        return;
    m_process.write("quit\n");
    if (!m_process.waitForFinished(m_config.shutdownTimeoutMs))
        m_process.kill();
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
    if (m_config.resetBeforeLoad)
        m_commands.enqueue(QStringLiteral("reset"));
    for (const QString& command : m_config.preLoadCommands)
        m_commands.enqueue(command);
    if (m_config.loadImage && !m_config.programImage.isEmpty())
        m_commands.enqueue(QStringLiteral("program %1").arg(quotedMdbPath(m_config.programImage)));
    for (const QString& command : m_config.postLoadCommands)
        m_commands.enqueue(command);
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
    m_outputBuffer += text;
    if (m_outputBuffer.size() > 8192)
        m_outputBuffer = m_outputBuffer.right(8192);
    emit outputReceived(text);
    if (m_waitingForPrompt && outputHasPrompt()) {
        m_waitingForPrompt = false;
        m_outputBuffer.clear();
        dispatchNextCommand();
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
    emit errorOccurred(QStringLiteral("Timed out waiting for the MPLAB MDB prompt."));
    stop();
}
