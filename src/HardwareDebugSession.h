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

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

#include "HardwareServerConfig.h"
#include "GdbServerProcess.h"
#include "DebugSession.h" // Includes DebuggerSession

// ============================================================================
// Hardware debug session orchestrator
// ============================================================================
// Manages the lifecycle of a hardware debugging session:
//   1. Validate configuration
//   2. Start GDB server (via GdbServerProcess)
//   3. Wait for server ready
//   4. Start GDB (via existing DebuggerSession)
//   5. Execute GDB/MI sequence (load, connect, reset, breakpoint, run)
//   6. Handle shutdown gracefully
// ============================================================================

class HardwareDebugSession final : public QObject
{
    Q_OBJECT

public:
    enum class SessionState {
        Idle,
        ConfigInvalid,
        ServerStarting,
        ServerReady,
        GdbStarting,
        GdbConnected,
        TargetHalted,
        Downloading,
        TargetReady,
        Running,
        Disconnecting,
        Error
    };

    explicit HardwareDebugSession(QObject* parent = nullptr);
    ~HardwareDebugSession() override;

    // Start a hardware debugging session
    void startSession(const HardwareDebugConfiguration& config, DebuggerSession* gdbSession);

    // Stop the session gracefully
    void stopSession();

    // Current state
    [[nodiscard]] SessionState sessionState() const { return m_sessionState; }

    // Access to server process (for test configuration)
    [[nodiscard]] GdbServerProcess* serverProcess() { return m_serverProcess.get(); }

    // Generation counter for session identity
    [[nodiscard]] int generation() const { return m_generation; }

signals:
    void sessionStateChanged(HardwareDebugSession::SessionState newState);
    void sessionStarted();
    void sessionStopped();
    void sessionError(const QString& message);
    void serverOutput(const QString& text);
    void debugOutput(const QString& text);

private slots:
    void onServerStateChanged(GdbServerState newState);
    void onServerReady();
    void onServerError(const QString& message);
    void onServerOutput(const QString& text);
    void onServerFinished(int exitCode, QProcess::ExitStatus status);

    void onGdbTargetStarted();
    void onGdbTargetStopped();
    void onGdbTargetExited(int exitCode);

    void onGdbOutput(const QString& text);

    // GDB/MI sequence steps
    void stepLoadSymbols(const QString&);
    void stepPreConnect(const QString&);
    void stepTargetSelect(const QString&);
    void stepPostConnect(const QString&);
    void stepResetHalt(const QString&);
    void stepPreLoad(const QString&);
    void stepDownload(const QString&);
    void stepPostLoad(const QString&);
    void stepInsertBreakpoint(const QString&);
    void stepRun(const QString&);

private:
    void setSessionState(SessionState newState);
    void executeGdbSequence();
    void sendCommand(const QString& cmd,
                     std::function<void(const QString&)> cb = nullptr);
    void abortSession(const QString& reason);

    // State
    SessionState m_sessionState = SessionState::Idle;
    int m_generation = 0;

    // Configuration
    HardwareDebugConfiguration m_config;

    // Owned server process
    std::unique_ptr<GdbServerProcess> m_serverProcess;

    // Reference to existing GDB session (not owned)
    DebuggerSession* m_gdbSession = nullptr;

    // Sequence tracking
    int m_sequenceStep = 0;
    using StepHandler = std::function<void(const QString&)>;
    QVector<StepHandler> m_sequenceSteps;

    // Output prefix helper
    QString m_prefix;
};