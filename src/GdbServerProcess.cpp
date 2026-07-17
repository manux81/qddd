/*
 * Copyright (c) [2026], Manuele Conti
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Manuele Conti nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "GdbServerProcess.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

// ============================================================================
// Constructor / Destructor
// ============================================================================

GdbServerProcess::GdbServerProcess(QObject* parent)
    : QObject(parent)
    , m_tcpProbeTimer(this)
{
    m_startupTimer.setSingleShot(true);
    m_shutdownTimer.setSingleShot(true);
    m_tcpProbeTimer.setSingleShot(false); // periodic for TCP probe

    connect(&m_process, &QProcess::started,
            this, &GdbServerProcess::onProcessStarted);
    connect(&m_process,
            QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this, &GdbServerProcess::onProcessErrorOccurred);
    connect(&m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &GdbServerProcess::onProcessFinished);
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &GdbServerProcess::onStdoutReady);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &GdbServerProcess::onStderrReady);

    connect(&m_startupTimer, &QTimer::timeout,
            this, &GdbServerProcess::onStartupTimeout);
    connect(&m_shutdownTimer, &QTimer::timeout,
            this, &GdbServerProcess::onShutdownTimeout);
}

GdbServerProcess::~GdbServerProcess()
{
    stopTcpProbe();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        // Don't waitForFinished in destructor - it blocks the event loop
        // and can cause crashes if called from within signal handlers.
        // The process will be reaped by the kernel.
    }
}

// ============================================================================
// Start
// ============================================================================

void GdbServerProcess::start(const HardwareDebugConfiguration& config)
{
    // Prevent duplicate starts
    if (m_state != GdbServerState::Stopped && m_state != GdbServerState::Failed) {
        qWarning() << "[GDB SERVER] Ignoring start request: state =" << static_cast<int>(m_state);
        return;
    }

    ++m_generation;
    m_shutdownRequested = false;
    m_config = config;
    m_logBuffer.clear();

    // Validate configuration
    const auto vr = config.validate();
    if (!vr.valid) {
        const QString msg = QStringLiteral("Configuration validation failed:\n%1")
            .arg(vr.errors.join(QStringLiteral("\n")));
        qWarning().noquote() << "[GDB SERVER]" << msg;
        emit errorOccurred(msg);
        setState(GdbServerState::Failed);
        return;
    }

    // Prepare ready detection
    m_usePattern = !config.readyPattern.isEmpty();
    m_useTcpProbe = false;

    if (m_usePattern) {
        m_readyRegex = QRegularExpression(config.readyPattern);
        if (!m_readyRegex.isValid()) {
            qWarning().noquote()
                << "[GDB SERVER] Invalid ready pattern:" << config.readyPattern
                << "- falling back to TCP probe";
            m_usePattern = false;
            m_useTcpProbe = true;
        }
    } else {
        m_useTcpProbe = true;
    }

    // Assemble command line
    const QString program = config.serverExecutable;
    const QStringList args = config.generateServerArguments();

    // Log the command
    appendLog(QStringLiteral("Starting: %1 %2")
              .arg(program, args.join(QStringLiteral(" "))));
    if (!config.workingDirectory.isEmpty()) {
        appendLog(QStringLiteral("Working directory: %1").arg(config.workingDirectory));
    }
    appendLog(QStringLiteral("Host: %1 Port: %2").arg(config.host).arg(config.port));

    emit outputReceived(
        QStringLiteral("[GDB SERVER] Starting: %1 %2\n")
        .arg(program, args.join(QStringLiteral(" "))));

    // Apply environment
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList envKeys = config.serverEnvironment.keys();
    for (const QString& key : envKeys) {
        env.insert(key, config.serverEnvironment.value(key));
    }
    m_process.setProcessEnvironment(env);

    // Set working directory if specified
    if (!config.workingDirectory.isEmpty()) {
        m_process.setWorkingDirectory(config.workingDirectory);
    }

    // Start the process (no shell!)
    m_process.setProgram(program);
    m_process.setArguments(args);
    m_process.start();

    setState(GdbServerState::Starting);

    // Start startup timeout
    m_startupTimer.start(config.startupTimeoutMs);
}

// ============================================================================
// Stop / Kill
// ============================================================================

void GdbServerProcess::stop()
{
    if (m_state == GdbServerState::Stopped || m_state == GdbServerState::Stopping)
        return;

    m_shutdownRequested = true;
    setState(GdbServerState::Stopping);

    // Stop TCP probe if running
    stopTcpProbe();

    // Cancel startup timer
    m_startupTimer.stop();

    appendLog(QStringLiteral("Stopping server..."));

    if (m_process.state() != QProcess::NotRunning) {
        // First try graceful terminate
        m_process.terminate();
        m_shutdownTimer.start(m_config.shutdownTimeoutMs);
    } else {
        // Process already finished
        setState(GdbServerState::Stopped);
    }
}

void GdbServerProcess::kill()
{
    m_shutdownRequested = true;
    stopTcpProbe();
    m_startupTimer.stop();
    m_shutdownTimer.stop();

    if (m_process.state() != QProcess::NotRunning) {
        appendLog(QStringLiteral("Force killing server..."));
        m_process.kill();
    }

    setState(GdbServerState::Stopped);
}

// ============================================================================
// Log access
// ============================================================================

QString GdbServerProcess::recentLog() const
{
    return m_logBuffer.join(QStringLiteral("\n"));
}

// ============================================================================
// State management
// ============================================================================

void GdbServerProcess::setState(GdbServerState newState)
{
    if (m_state == newState)
        return;
    m_state = newState;
    emit stateChanged(newState);
}

// ============================================================================
// Process slots
// ============================================================================

void GdbServerProcess::onProcessStarted()
{
    appendLog(QStringLiteral("Process started (PID: %1)").arg(m_process.processId()));
    emit outputReceived(
        QStringLiteral("[GDB SERVER] Process started (PID: %1)\n")
        .arg(m_process.processId()));

    // If no pattern-based ready detection, start TCP probing
    if (!m_usePattern) {
        m_useTcpProbe = true;
        // Slight delay before first probe
        QTimer::singleShot(500, this, &GdbServerProcess::startTcpProbe);
    }
}

void GdbServerProcess::onProcessErrorOccurred(QProcess::ProcessError error)
{
    QString msg;
    switch (error) {
    case QProcess::FailedToStart:
        msg = QStringLiteral("Failed to start: %1").arg(m_config.serverExecutable);
        break;
    case QProcess::Crashed:
        msg = QStringLiteral("Process crashed");
        break;
    case QProcess::Timedout:
        msg = QStringLiteral("Process timed out");
        break;
    case QProcess::WriteError:
        msg = QStringLiteral("Write error");
        break;
    case QProcess::ReadError:
        msg = QStringLiteral("Read error");
        break;
    default:
        msg = QStringLiteral("Unknown error (%1)").arg(static_cast<int>(error));
        break;
    }

    appendLog(QStringLiteral("Error: %1").arg(msg));
    emit errorOutputReceived(
        QStringLiteral("[GDB SERVER ERR] %1 (exit code: %2)\n")
        .arg(msg)
        .arg(m_process.exitCode()));

    // Only transition to Failed if we're still in Starting state
    if (m_state == GdbServerState::Starting) {
        m_startupTimer.stop();
        stopTcpProbe();
        emit errorOccurred(msg);
        setState(GdbServerState::Failed);
    }
}

void GdbServerProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_startupTimer.stop();
    m_shutdownTimer.stop();
    stopTcpProbe();

    const QString exitDesc = (status == QProcess::NormalExit)
        ? QStringLiteral("exit code %1").arg(exitCode)
        : QStringLiteral("crashed (exit code %1)").arg(exitCode);

    appendLog(QStringLiteral("Process finished: %1").arg(exitDesc));
    emit outputReceived(
        QStringLiteral("[GDB SERVER] Process finished (%1)\n").arg(exitDesc));

    if (m_state == GdbServerState::Starting) {
        // Process died before becoming ready
        const QString msg = QStringLiteral("Server terminated before becoming ready (%1)")
            .arg(exitDesc);
        emit errorOccurred(msg);
        setState(GdbServerState::Failed);
    } else if (m_state == GdbServerState::Ready || m_state == GdbServerState::Stopping) {
        setState(GdbServerState::Stopped);
    } else {
        setState(GdbServerState::Stopped);
    }

    emit finished(exitCode, status);
}

void GdbServerProcess::onStdoutReady()
{
    const QByteArray data = m_process.readAllStandardOutput();
    const QString text = QString::fromUtf8(data);
    appendLog(QStringLiteral("[stdout] %1").arg(text.trimmed()));
    emit outputReceived(
        QStringLiteral("[GDB SERVER OUT] %1").arg(text));

    // Check for ready pattern
    if (m_usePattern && m_state == GdbServerState::Starting) {
        checkReadyPattern(text);
    }
}

void GdbServerProcess::onStderrReady()
{
    const QByteArray data = m_process.readAllStandardError();
    const QString text = QString::fromUtf8(data);
    appendLog(QStringLiteral("[stderr] %1").arg(text.trimmed()));
    emit errorOutputReceived(
        QStringLiteral("[GDB SERVER ERR] %1").arg(text));

    // Check for ready pattern on stderr too
    if (m_usePattern && m_state == GdbServerState::Starting) {
        checkReadyPattern(text);
    }
}

// ============================================================================
// Timeout handlers
// ============================================================================

void GdbServerProcess::onStartupTimeout()
{
    stopTcpProbe();

    if (m_state != GdbServerState::Starting)
        return;

    const QString msg = QStringLiteral(
        "Startup timeout after %1 ms.\n"
        "Executable: %2\n"
        "Last output:\n%3")
        .arg(m_config.startupTimeoutMs)
        .arg(m_config.serverExecutable)
        .arg(recentLog());

    appendLog(QStringLiteral("Startup timeout"));
    emit errorOccurred(msg);
    emit errorOutputReceived(
        QStringLiteral("[GDB SERVER ERR] Startup timeout (%1 ms)\n")
        .arg(m_config.startupTimeoutMs));

    setState(GdbServerState::Failed);
}

void GdbServerProcess::onShutdownTimeout()
{
    appendLog(QStringLiteral("Shutdown timeout, force killing..."));
    emit outputReceived(
        QStringLiteral("[GDB SERVER] Shutdown timeout, force killing\n"));

    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
    }

    setState(GdbServerState::Stopped);
}

// ============================================================================
// TCP probe (async)
// ============================================================================

void GdbServerProcess::startTcpProbe()
{
    if (m_state != GdbServerState::Starting)
        return;

    if (!m_useTcpProbe)
        return;

    // Clean up previous probe
    stopTcpProbe();

    m_tcpProbe = new QTcpSocket(this);
    connect(m_tcpProbe, &QTcpSocket::connected,
            this, &GdbServerProcess::onTcpProbeConnected);
    connect(m_tcpProbe, &QAbstractSocket::errorOccurred,
            this, &GdbServerProcess::onTcpProbeError);

    // Try to connect (non-blocking)
    m_tcpProbe->connectToHost(m_config.host, m_config.port);
}

void GdbServerProcess::stopTcpProbe()
{
    m_tcpProbeTimer.stop();
    if (m_tcpProbe) {
        m_tcpProbe->disconnect();
        m_tcpProbe->abort();
        m_tcpProbe->deleteLater();
        m_tcpProbe = nullptr;
    }
}

void GdbServerProcess::onTcpProbeConnected()
{
    if (m_state != GdbServerState::Starting) {
        stopTcpProbe();
        return;
    }

    appendLog(QStringLiteral("TCP probe succeeded - server is ready"));
    emit outputReceived(
        QStringLiteral("[GDB SERVER] Server is ready (TCP probe OK on %1:%2)\n")
        .arg(m_config.host).arg(m_config.port));

    stopTcpProbe();
    m_startupTimer.stop();

    setState(GdbServerState::Ready);
    emit ready();
}

void GdbServerProcess::onTcpProbeError(QAbstractSocket::SocketError /*error*/)
{
    if (m_state != GdbServerState::Starting) {
        stopTcpProbe();
        return;
    }

    // Connection refused / not ready yet - retry after delay
    stopTcpProbe();

    // Schedule another probe (unless startup timer expired)
    if (m_startupTimer.isActive()) {
        QTimer::singleShot(200, this, &GdbServerProcess::startTcpProbe);
    }
}

// ============================================================================
// Ready pattern matching
// ============================================================================

void GdbServerProcess::checkReadyPattern(const QString& text)
{
    if (m_state != GdbServerState::Starting)
        return;

    if (m_readyRegex.match(text).hasMatch()) {
        appendLog(QStringLiteral("Ready pattern matched in output"));
        emit outputReceived(
            QStringLiteral("[GDB SERVER] Server is ready (pattern match)\n"));

        m_useTcpProbe = false;
        m_startupTimer.stop();
        stopTcpProbe();

        setState(GdbServerState::Ready);
        emit ready();
    }
}

// ============================================================================
// Log helper
// ============================================================================

void GdbServerProcess::appendLog(const QString& line)
{
    m_logBuffer.append(line);
    while (m_logBuffer.size() > kMaxLogLines) {
        m_logBuffer.removeFirst();
    }
}