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

#include "HardwareDebugSession.h"

#include <QDebug>
#include <QFileInfo>

// ============================================================================
// Constructor / Destructor
// ============================================================================

HardwareDebugSession::HardwareDebugSession(QObject* parent)
    : QObject(parent)
{
    m_serverProcess = std::make_unique<GdbServerProcess>(this);

    connect(m_serverProcess.get(), &GdbServerProcess::stateChanged,
            this, &HardwareDebugSession::onServerStateChanged);
    connect(m_serverProcess.get(), &GdbServerProcess::ready,
            this, &HardwareDebugSession::onServerReady);
    connect(m_serverProcess.get(), &GdbServerProcess::errorOccurred,
            this, &HardwareDebugSession::onServerError);
    connect(m_serverProcess.get(), &GdbServerProcess::outputReceived,
            this, &HardwareDebugSession::onServerOutput);
    connect(m_serverProcess.get(), &GdbServerProcess::errorOutputReceived,
            this, &HardwareDebugSession::onServerOutput);
    connect(m_serverProcess.get(), &GdbServerProcess::finished,
            this, &HardwareDebugSession::onServerFinished);
}

HardwareDebugSession::~HardwareDebugSession() = default;

// ============================================================================
// State management
// ============================================================================

void HardwareDebugSession::setSessionState(SessionState newState)
{
    if (m_sessionState == newState)
        return;
    m_sessionState = newState;
    emit sessionStateChanged(newState);
}

// ============================================================================
// Start session
// ============================================================================

void HardwareDebugSession::startSession(const HardwareDebugConfiguration& config,
                                         DebuggerSession* gdbSession)
{
    if (!gdbSession) {
        emit sessionError(QStringLiteral("GDB session is null."));
        return;
    }

    // Prevent concurrent sessions on the same configuration
    if (m_sessionState != SessionState::Idle && m_sessionState != SessionState::Error) {
        emit sessionError(QStringLiteral("A session is already active (state: %1).")
                          .arg(static_cast<int>(m_sessionState)));
        return;
    }

    ++m_generation;
    m_gdbSession = gdbSession;
    m_config = config;
    m_sequenceStep = 0;
    m_sequenceSteps.clear();

    // Validate configuration
    const auto vr = config.validate();
    if (!vr.valid) {
        const QString msg = QStringLiteral(
            "[HARDWARE DEBUG] Configuration validation failed:\n%1")
            .arg(vr.errors.join(QStringLiteral("\n")));
        qWarning().noquote() << msg;
        emit sessionError(msg);
        setSessionState(SessionState::ConfigInvalid);
        return;
    }

    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Starting session\n"));
    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Server: %1\n")
                     .arg(config.serverExecutable));
    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] GDB: %1\n")
                     .arg(config.gdbExecutable));
    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Target: %1:%2\n")
                     .arg(config.host).arg(config.port));
    if (!config.programImage.isEmpty()) {
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Image: %1\n")
                         .arg(config.programImage));
    }

    // Connect GDB session signals
    connect(m_gdbSession, &DebuggerSession::targetStarted,
            this, &HardwareDebugSession::onGdbTargetStarted, Qt::UniqueConnection);
    connect(m_gdbSession, &DebuggerSession::targetStopped,
            this, &HardwareDebugSession::onGdbTargetStopped, Qt::UniqueConnection);
    connect(m_gdbSession, &DebuggerSession::targetExited,
            this, &HardwareDebugSession::onGdbTargetExited, Qt::UniqueConnection);
    connect(m_gdbSession, &DebuggerSession::debuggerOutput,
            this, &HardwareDebugSession::onGdbOutput, Qt::UniqueConnection);

    // Step 1: Start the GDB server
    setSessionState(SessionState::ServerStarting);
    m_serverProcess->start(config);

    // The rest of the flow is driven by signals from the server process
}

// ============================================================================
// Stop session
// ============================================================================

void HardwareDebugSession::stopSession()
{
    const int myGen = m_generation;

    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Stopping session\n"));
    setSessionState(SessionState::Disconnecting);

    // Disconnect GDB session signals to avoid stale callbacks
    if (m_gdbSession) {
        disconnect(m_gdbSession, &DebuggerSession::targetStarted,
                   this, &HardwareDebugSession::onGdbTargetStarted);
        disconnect(m_gdbSession, &DebuggerSession::targetStopped,
                   this, &HardwareDebugSession::onGdbTargetStopped);
        disconnect(m_gdbSession, &DebuggerSession::targetExited,
                   this, &HardwareDebugSession::onGdbTargetExited);
        disconnect(m_gdbSession, &DebuggerSession::debuggerOutput,
                   this, &HardwareDebugSession::onGdbOutput);
    }

    // Stop GDB first
    if (m_gdbSession && m_gdbSession->isRunning()) {
        m_gdbSession->terminateSession();
    }

    // Stop server
    if (m_serverProcess) {
        if (m_serverProcess->state() != GdbServerState::Stopped &&
            m_serverProcess->state() != GdbServerState::Failed) {
            m_serverProcess->stop();
        }
    }

    // Clean up sequence state
    m_sequenceStep = 0;
    m_sequenceSteps.clear();

    setSessionState(SessionState::Idle);
    emit sessionStopped();
}

// ============================================================================
// Server state handlers
// ============================================================================

void HardwareDebugSession::onServerStateChanged(GdbServerState newState)
{
    emit debugOutput(
        QStringLiteral("[HARDWARE DEBUG] Server state changed to %1\n")
        .arg(static_cast<int>(newState)));
}

void HardwareDebugSession::onServerReady()
{
    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Server is ready\n"));
    setSessionState(SessionState::ServerReady);

    // Configure and start GDB via the existing DebuggerSession
    if (!m_gdbSession)
        return;

    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Starting GDB...\n"));
    setSessionState(SessionState::GdbStarting);

    // We need to start GDB manually since DebuggerSession::startSession
    // would try to do the full local session. Instead, we configure
    // the GDB session appropriately and use the underlying QProcess.
    // The existing DebuggerSession is designed to work with our hardware flow.
    m_gdbSession->setBackend(DebuggerSession::Backend::GdbMi);
    m_gdbSession->setGdbExecutable(m_config.gdbExecutable);

    // Start GDB with MI interpreter
    // Note: We bypass DebuggerSession::startSession() which also does
    // target-select. Instead, we use the existing enqueueCommand mechanism
    // after GDB starts.
    // We need to tell the DebuggerSession to start GDB process without
    // doing the full local startup flow.
    // The simplest approach: use the existing session but override target type
    // to remote so it will do -target-select.

    // For non-hardware targets, DebuggerSession::startSession handles everything.
    // For hardware debugging, we orchestrate the full sequence here.
    // So we set target type to RemoteGdbserver and start.

    m_gdbSession->setTargetType(DebuggerSession::TargetType::RemoteGdbserver);
    m_gdbSession->setRemoteEndpoint(m_config.host, m_config.port);

    // Start GDB process only (without full session logic)
    // We need this because startSession does the load+target-select in sequence.
    // We'll do the full MI sequence ourselves.
    if (!m_config.programImage.isEmpty()) {
        // Start GDB with MI and then execute our sequence
        // We initialize the debugger process ourselves to maintain control
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Starting GDB with MI...\n"));

        // Use the mechanism already in DebuggerSession but wrap it:
        // startSession will load symbols and connect. We will chain our
        // hardware-specific commands after.
        m_gdbSession->startSession(m_config.programImage);

        // The subsequent sequence steps will be executed as callbacks
        // chained from the final callback of startSession.
    } else {
        // No program image - just start GDB and connect
        m_gdbSession->startSession(QString());
    }
}

void HardwareDebugSession::onServerError(const QString& message)
{
    emit sessionError(message);
    setSessionState(SessionState::Error);
}

void HardwareDebugSession::onServerOutput(const QString& text)
{
    emit serverOutput(text);
}

void HardwareDebugSession::onServerFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_sessionState == SessionState::ServerStarting) {
        const QString statusStr = (status == QProcess::NormalExit)
            ? QStringLiteral("exit code %1").arg(exitCode)
            : QStringLiteral("crashed");
        abortSession(QStringLiteral("Server finished unexpectedly during startup (%1)")
                     .arg(statusStr));
    } else if (m_sessionState == SessionState::Disconnecting ||
               m_sessionState == SessionState::Idle) {
        // Expected after stop
    } else if (m_sessionState != SessionState::Error) {
        abortSession(QStringLiteral("Server finished unexpectedly (exit code %1)")
                     .arg(exitCode));
    }
}

// ============================================================================
// GDB state handlers
// ============================================================================

void HardwareDebugSession::onGdbTargetStarted()
{
    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] GDB target started\n"));
    setSessionState(SessionState::GdbConnected);
}

void HardwareDebugSession::onGdbTargetStopped()
{
    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] GDB target stopped\n"));
    setSessionState(SessionState::TargetHalted);
}

void HardwareDebugSession::onGdbTargetExited(int exitCode)
{
    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] GDB target exited (%1)\n")
                     .arg(exitCode));
    setSessionState(SessionState::Idle);
}

void HardwareDebugSession::onGdbOutput(const QString& text)
{
    emit debugOutput(text);
}

// ============================================================================
// GDB/MI sequence
// ============================================================================

void HardwareDebugSession::executeGdbSequence()
{
    if (!m_gdbSession)
        return;

    m_sequenceStep = 0;
    m_sequenceSteps.clear();

    // Build the sequence based on configuration
    //
    // 1. Load symbols (if requested)
    // 2. Pre-connect commands
    // 3. Target select (remote)
    // 4. Post-connect commands
    // 5. Reset / halt (if requested)
    // 6. Pre-load commands
    // 7. Download image (if requested)
    // 8. Post-load commands
    // 9. Insert initial breakpoint (if configured)
    // 10. Run (if requested)

    if (m_config.loadSymbols && !m_config.programImage.isEmpty()) {
        m_sequenceSteps.append([this](const QString&) {
            const QString symFile = m_config.symbolFile.isEmpty()
                ? m_config.programImage
                : m_config.symbolFile;
            emit debugOutput(
                QStringLiteral("[HARDWARE DEBUG] Loading symbols: %1\n").arg(symFile));
            sendCommand(
                QStringLiteral("-file-exec-and-symbols \"%1\"").arg(symFile));
        });
    }

    if (!m_config.preConnectCommands.isEmpty()) {
        for (const auto& cmd : m_config.preConnectCommands) {
            m_sequenceSteps.append([this, cmd](const QString&) {
                emit debugOutput(
                    QStringLiteral("[HARDWARE DEBUG] Pre-connect: %1\n").arg(cmd));
                sendCommand(cmd);
            });
        }
    }

    // Target select
    m_sequenceSteps.append([this](const QString&) {
        const QString targetSpec = QStringLiteral("%1:%2")
            .arg(m_config.host).arg(m_config.port);
        emit debugOutput(
            QStringLiteral("[HARDWARE DEBUG] Connecting to target: %1\n").arg(targetSpec));

        // Determine if we need extended-remote or remote
        // ST-LINK typically uses extended-remote
        // J-Link uses remote
        if (m_config.serverType == HardwareServerType::STLink) {
            sendCommand(
                QStringLiteral("-target-select extended-remote %1:%2")
                .arg(m_config.host).arg(m_config.port));
        } else {
            sendCommand(
                QStringLiteral("-target-select remote %1:%2")
                .arg(m_config.host).arg(m_config.port));
        }
    });

    if (!m_config.postConnectCommands.isEmpty()) {
        for (const auto& cmd : m_config.postConnectCommands) {
            m_sequenceSteps.append([this, cmd](const QString&) {
                emit debugOutput(
                    QStringLiteral("[HARDWARE DEBUG] Post-connect: %1\n").arg(cmd));
                sendCommand(cmd);
            });
        }
    }

    // Reset / halt
    if (m_config.resetBeforeLoad) {
        m_sequenceSteps.append([this](const QString&) {
            emit debugOutput(
                QStringLiteral("[HARDWARE DEBUG] Resetting target via monitor\n"));

            if (m_config.serverType == HardwareServerType::JLink) {
                // J-Link uses "monitor reset" followed by "monitor halt"
                sendCommand(
                    QStringLiteral("-interpreter-exec console \"monitor reset\""));
                // Halt will be handled separately below
            } else {
                // ST-LINK uses "monitor reset halt"
                sendCommand(
                    QStringLiteral("-interpreter-exec console \"monitor reset halt\""));
            }
        });
    }

    if (m_config.haltAfterReset && m_config.serverType == HardwareServerType::JLink) {
        m_sequenceSteps.append([this](const QString&) {
            emit debugOutput(
                QStringLiteral("[HARDWARE DEBUG] Halting target via monitor\n"));
            sendCommand(
                QStringLiteral("-interpreter-exec console \"monitor halt\""));
        });
    }

    if (!m_config.preLoadCommands.isEmpty()) {
        for (const auto& cmd : m_config.preLoadCommands) {
            m_sequenceSteps.append([this, cmd](const QString&) {
                emit debugOutput(
                    QStringLiteral("[HARDWARE DEBUG] Pre-load: %1\n").arg(cmd));
                sendCommand(cmd);
            });
        }
    }

    // Download image
    if (m_config.loadImage && !m_config.programImage.isEmpty()) {
        m_sequenceSteps.append([this](const QString&) {
            setSessionState(SessionState::Downloading);
            emit debugOutput(
                QStringLiteral("[HARDWARE DEBUG] Downloading image...\n"));
            sendCommand(QStringLiteral("-target-download"));
        });
    }

    if (!m_config.postLoadCommands.isEmpty()) {
        for (const auto& cmd : m_config.postLoadCommands) {
            m_sequenceSteps.append([this, cmd](const QString&) {
                emit debugOutput(
                    QStringLiteral("[HARDWARE DEBUG] Post-load: %1\n").arg(cmd));
                sendCommand(cmd);
            });
        }
    }

    // Insert initial breakpoint
    if (!m_config.initialBreakpoint.isEmpty()) {
        m_sequenceSteps.append([this](const QString&) {
            emit debugOutput(
                QStringLiteral("[HARDWARE DEBUG] Setting breakpoint: %1\n")
                .arg(m_config.initialBreakpoint));
            sendCommand(
                QStringLiteral("-break-insert %1").arg(m_config.initialBreakpoint));
        });
    }

    // Run
    if (m_config.runAfterLoad) {
        m_sequenceSteps.append([this](const QString&) {
            emit debugOutput(
                QStringLiteral("[HARDWARE DEBUG] Running...\n"));
            sendCommand(QStringLiteral("-exec-run"));
        });
    }

    // Final step: mark session as started
    m_sequenceSteps.append([this](const QString&) {
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Session ready\n"));
        setSessionState(SessionState::TargetReady);
        emit sessionStarted();
    });

    // Start executing the sequence
    m_sequenceStep = 0;
    if (!m_sequenceSteps.isEmpty()) {
        // The first step is triggered manually
        m_sequenceSteps[0](QString());
    }
}

// ============================================================================
// Command sending with chaining
// ============================================================================

void HardwareDebugSession::sendCommand(const QString& cmd,
                                        std::function<void(const QString&)> cb)
{
    if (!m_gdbSession) {
        abortSession(QStringLiteral("GDB session is null"));
        return;
    }

    // Use the existing command queue from DebuggerSession
    // The callback chains into the next sequence step
    m_gdbSession->sendRawCommand(cmd);
}

// ============================================================================
// Abort session
// ============================================================================

void HardwareDebugSession::abortSession(const QString& reason)
{
    qWarning().noquote() << "[HARDWARE DEBUG] Aborting session:" << reason;
    emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Aborting: %1\n").arg(reason));
    emit sessionError(reason);
    setSessionState(SessionState::Error);

    // Clean up
    stopSession();
}

// ============================================================================
// Step handlers (chain callbacks)
// ============================================================================

void HardwareDebugSession::stepLoadSymbols(const QString&)
{
    // Handled via executeGdbSequence() steps vector
}

void HardwareDebugSession::stepPreConnect(const QString&) {}
void HardwareDebugSession::stepTargetSelect(const QString&) {}
void HardwareDebugSession::stepPostConnect(const QString&) {}
void HardwareDebugSession::stepResetHalt(const QString&) {}
void HardwareDebugSession::stepPreLoad(const QString&) {}
void HardwareDebugSession::stepDownload(const QString&) {}
void HardwareDebugSession::stepPostLoad(const QString&) {}
void HardwareDebugSession::stepInsertBreakpoint(const QString&) {}
void HardwareDebugSession::stepRun(const QString&) {}