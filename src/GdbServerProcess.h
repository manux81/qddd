/*
 * Copyright (c) 2026, Manuele Conti
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

#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QTcpSocket>
#include <QString>
#include <QStringList>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QElapsedTimer>

#include "HardwareServerConfig.h"

// ============================================================================
// GDB server process state
// ============================================================================

enum class GdbServerState {
    Stopped,
    Starting,
    Ready,
    Stopping,
    Failed
};

// ============================================================================
// Asynchronous GDB server process manager
// ============================================================================

class GdbServerProcess final : public QObject
{
    Q_OBJECT

public:
    explicit GdbServerProcess(QObject* parent = nullptr);
    ~GdbServerProcess() override;

    // Start the server with the given configuration
    void start(const HardwareDebugConfiguration& config);

    // Stop the server gracefully (terminate, then kill after timeout)
    void stop();

    // Force kill immediately
    void kill();

    // Current state
    [[nodiscard]] GdbServerState state() const { return m_state; }

    // Access to recent log buffer for diagnostics
    [[nodiscard]] QString recentLog() const;

    // Session generation counter to ignore stale signals
    [[nodiscard]] int generation() const { return m_generation; }

signals:
    void stateChanged(GdbServerState newState);
    void ready();
    void outputReceived(const QString& text);
    void errorOutputReceived(const QString& text);
    void errorOccurred(const QString& message);
    void finished(int exitCode, QProcess::ExitStatus status);

private slots:
    void onProcessStarted();
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onStdoutReady();
    void onStderrReady();
    void onStartupTimeout();
    void onShutdownTimeout();
    void onTcpProbeConnected();
    void onTcpProbeError(QAbstractSocket::SocketError error);

private:
    void setState(GdbServerState newState);
    void startTcpProbe();
    void stopTcpProbe();
    void checkReadyPattern(const QString& line);
    void appendLog(const QString& line);

    QProcess m_process;
    QTimer m_startupTimer;
    QTimer m_shutdownTimer;
    QTcpSocket* m_tcpProbe = nullptr;
    QTimer m_tcpProbeTimer;

    GdbServerState m_state = GdbServerState::Stopped;
    int m_generation = 0;

    // Configuration snapshot
    HardwareDebugConfiguration m_config;

    // Ready detection
    QRegularExpression m_readyRegex;
    bool m_useTcpProbe = false;
    bool m_usePattern = false;

    // Log buffer (last 100 lines)
    QStringList m_logBuffer;
    static constexpr int kMaxLogLines = 100;

    // Shutdown tracking
    bool m_shutdownRequested = false;
};