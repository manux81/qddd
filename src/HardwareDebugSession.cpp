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
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

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

    m_mdbProcess = std::make_unique<MdbProcess>(this);
    connect(m_mdbProcess.get(), &MdbProcess::outputReceived,
            this, &HardwareDebugSession::onServerOutput);
    connect(m_mdbProcess.get(), &MdbProcess::errorOccurred,
            this, &HardwareDebugSession::onServerError);
    connect(m_mdbProcess.get(), &MdbProcess::ready, this, [this] {
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] MPLAB MDB session ready\n"));
        setSessionState(m_config.runAfterLoad ? SessionState::Running : SessionState::TargetReady);
        emit sessionStarted();
    });
    connect(m_mdbProcess.get(), &MdbProcess::targetRunning, this, [this] {
        setSessionState(SessionState::Running);
    });
    connect(m_mdbProcess.get(), &MdbProcess::targetStopped, this, [this] {
        setSessionState(SessionState::TargetHalted);
        refreshMdbVariables();
    });
    connect(m_mdbProcess.get(), &MdbProcess::sourceLocation, this,
            [this](const QString& file, int line) {
        emit stoppedAt(file, line, QString());
        setSessionState(SessionState::TargetHalted);
        refreshMdbVariables();
    });
    connect(m_mdbProcess.get(), &MdbProcess::commandFinished, this,
            [this](const QString& command, const QString& response) {
        static const QRegularExpression addedPattern(
            QStringLiteral("Breakpoint\\s+(\\d+)\\s+at"),
            QRegularExpression::CaseInsensitiveOption);
        if (command.startsWith(QStringLiteral("break "), Qt::CaseInsensitive)) {
            const auto match = addedPattern.match(response);
            if (!match.hasMatch())
                return;
            const QString location = command.mid(6).trimmed();
            const int separator = location.lastIndexOf(QLatin1Char(':'));
            if (separator <= 0)
                return;
            QString file = location.left(separator);
            if (file.startsWith(QLatin1Char('"')) && file.endsWith(QLatin1Char('"')))
                file = file.mid(1, file.size() - 2);
            bool ok = false;
            const int line = location.mid(separator + 1).toInt(&ok);
            if (!ok)
                return;
            file = QFileInfo(file).absoluteFilePath();
            const QString key = file + QLatin1Char(':') + QString::number(line);
            m_mdbBreakpointIds.insert(key, match.captured(1).toInt());
            m_mdbBreakpointLines[file].insert(line);
            emit breakpointLinesChanged(file, m_mdbBreakpointLines.value(file));
        } else if (command.startsWith(QStringLiteral("print /a "), Qt::CaseInsensitive)) {
            const QString expression = command.mid(9).trimmed();
            if (!m_pendingMdbWrites.contains(expression))
                return;
            QString address;
            static const QRegularExpression prefixedAddress(
                QStringLiteral("\\b0x([0-9a-fA-F]+)\\b"));
            const auto prefixedMatch = prefixedAddress.match(response);
            if (prefixedMatch.hasMatch()) {
                address = prefixedMatch.captured(1);
            } else {
                // XC16/dsPIC commonly prints an address as two lines:
                // "symbol=\n2006" (without 0x).
                static const QRegularExpression bareAddress(
                    QStringLiteral("=\\s*([0-9a-fA-F]+)\\s*(?:[\\r\\n]|$)"));
                const auto bareMatch = bareAddress.match(response);
                if (bareMatch.hasMatch())
                    address = bareMatch.captured(1);
            }
            const QString value = m_pendingMdbWrites.take(expression);
            if (address.isEmpty()) {
                emit debugOutput(QStringLiteral("[MDB] Cannot resolve address of %1\n").arg(expression));
                return;
            }
            const QString writeCommand = QStringLiteral("write /r 0x%1 %2")
                .arg(address, value);
            m_pendingMdbWrites.insert(writeCommand, expression);
            m_mdbProcess->sendCommand(writeCommand);
        } else if (command.startsWith(QStringLiteral("write /r "), Qt::CaseInsensitive)) {
            // Re-read after an assignment so the tree and graphical display
            // reflect the value actually accepted by the target.
            const QString expression = m_pendingMdbWrites.take(command);
            if (!expression.isEmpty())
                m_mdbProcess->sendCommand(QStringLiteral("print %1").arg(expression));
        } else if (command.startsWith(QStringLiteral("print "), Qt::CaseInsensitive)) {
            const QString expression = command.mid(6).trimmed();
            const bool hoverEvaluation = m_mdbEvaluations.contains(expression);
            QString value;
            const QStringList lines = response.split(
                QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
            for (const QString& rawLine : lines) {
                const QString line = rawLine.trimmed();
                if (line.isEmpty() || line == QLatin1String(">") ||
                    line.startsWith(QLatin1String("print "), Qt::CaseInsensitive))
                    continue;
                value = line;
            }
            if (!value.isEmpty()) {
                if (value.contains(QStringLiteral("Symbol does not exist"), Qt::CaseInsensitive)) {
                    const auto callbacks = m_mdbEvaluations.take(expression);
                    for (const auto& callback : callbacks)
                        callback({}, {});
                    return;
                }
                const int equals = value.indexOf(QLatin1Char('='));
                const QString parsedValue = equals >= 0
                    ? value.mid(equals + 1).trimmed() : value;
                const auto callbacks = m_mdbEvaluations.take(expression);
                for (const auto& callback : callbacks)
                    callback(parsedValue, QString());
                // Hover evaluation is transient. It must not turn an arbitrary
                // identifier under the mouse into a watch/data-display item.
                if (!hoverEvaluation) {
                    m_mdbVariables[expression] = parsedValue;
                    publishMdbVariables();
                    emit mdbVariablesChanged();
                }
            }
        } else if (command.startsWith(QStringLiteral("backtrace full"), Qt::CaseInsensitive)) {
            QVector<StackFrame> frames;
            // MDB follows the GDB backtrace format, e.g.
            // "#0  function (...) at /path/file.c:123". Addresses and the
            // optional "in" token vary between device families.
            static const QRegularExpression framePattern(
                QStringLiteral("^\\s*#(\\d+)\\s+(?:(?:0x[0-9a-fA-F]+)\\s+(?:in\\s+)?)?([^\\s(]+).*?\\s(?:at|from)\\s+(.+?):(\\d+)\\s*$"),
                QRegularExpression::MultilineOption);
            auto frameMatches = framePattern.globalMatch(response);
            while (frameMatches.hasNext()) {
                const auto match = frameMatches.next();
                StackFrame frame;
                frame.level = match.captured(1);
                frame.function = match.captured(2);
                frame.file = match.captured(3).trimmed();
                frame.line = match.captured(4).toInt();
                frames.append(frame);
            }
            if (frames.isEmpty()) {
                // Some MDB/device combinations omit '#', parentheses, or
                // addresses and print "0 function at file.c:line".
                static const QRegularExpression compactFramePattern(
                    QStringLiteral("^\\s*(\\d+)\\s+([A-Za-z_][A-Za-z0-9_:]*)\\s+.*?(?:at|from)\\s+(.+?):(\\d+)\\s*$"),
                    QRegularExpression::MultilineOption);
                auto compactMatches = compactFramePattern.globalMatch(response);
                while (compactMatches.hasNext()) {
                    const auto match = compactMatches.next();
                    frames.append({match.captured(1), match.captured(2),
                                   match.captured(3).trimmed(), match.captured(4).toInt()});
                }
            }
            if (m_gdbSession)
                m_gdbSession->replaceExternalStackFrames(frames);

            static const QRegularExpression localPattern(
                QStringLiteral("^\\s*([A-Za-z_][A-Za-z0-9_]*(?:\\[[^]]+\\])?)\\s*=\\s*(.+?)\\s*$"),
                QRegularExpression::MultilineOption);
            auto matches = localPattern.globalMatch(response);
            while (matches.hasNext()) {
                const auto match = matches.next();
                if (!m_mdbWatches.contains(match.captured(1)))
                    m_mdbVariables[match.captured(1)] = match.captured(2);
            }
            emit mdbVariablesChanged();
            publishMdbVariables();
        } else if (command.startsWith(QStringLiteral("delete "), Qt::CaseInsensitive)) {
            bool ok = false;
            const int id = command.mid(7).trimmed().toInt(&ok);
            if (!ok)
                return;
            QString removedKey;
            for (auto it = m_mdbBreakpointIds.cbegin(); it != m_mdbBreakpointIds.cend(); ++it) {
                if (it.value() == id) { removedKey = it.key(); break; }
            }
            if (removedKey.isEmpty())
                return;
            m_mdbBreakpointIds.remove(removedKey);
            const int separator = removedKey.lastIndexOf(QLatin1Char(':'));
            const QString file = removedKey.left(separator);
            const int line = removedKey.mid(separator + 1).toInt();
            m_mdbBreakpointLines[file].remove(line);
            emit breakpointLinesChanged(file, m_mdbBreakpointLines.value(file));
        }
    });
    connect(m_mdbProcess.get(), &MdbProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus) {
        if (m_sessionState != SessionState::Disconnecting &&
            m_sessionState != SessionState::Idle &&
            m_sessionState != SessionState::Error) {
            abortSession(QStringLiteral("MDB finished unexpectedly (exit code %1)").arg(exitCode));
        }
    });
}

HardwareDebugSession::~HardwareDebugSession() = default;

void HardwareDebugSession::toggleBreakpoint(const QString& file, int line)
{
    if (!usesMdb() || !m_mdbProcess || !m_mdbProcess->isRunning() || line <= 0)
        return;
    const QString absoluteFile = QFileInfo(file).absoluteFilePath();
    const QString key = absoluteFile + QLatin1Char(':') + QString::number(line);
    const auto existing = m_mdbBreakpointIds.constFind(key);
    if (existing != m_mdbBreakpointIds.constEnd()) {
        m_mdbBreakpointLines[absoluteFile].remove(line);
        emit breakpointLinesChanged(absoluteFile, m_mdbBreakpointLines.value(absoluteFile));
        m_mdbProcess->sendCommand(QStringLiteral("delete %1").arg(existing.value()));
        return;
    }
    QString escaped = absoluteFile;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    if (escaped.contains(QLatin1Char(' ')))
        escaped = QStringLiteral("\"") + escaped + QStringLiteral("\"");
    m_mdbBreakpointLines[absoluteFile].insert(line);
    emit breakpointLinesChanged(absoluteFile, m_mdbBreakpointLines.value(absoluteFile));
    m_mdbProcess->sendCommand(QStringLiteral("break %1:%2").arg(escaped).arg(line));
}

void HardwareDebugSession::addMdbWatch(const QString& expression)
{
    const QString trimmed = expression.trimmed();
    if (trimmed.isEmpty() || m_mdbWatches.contains(trimmed))
        return;
    m_mdbWatches.append(trimmed);
    if (m_mdbProcess && m_mdbProcess->isRunning())
        m_mdbProcess->sendCommand(QStringLiteral("print %1").arg(trimmed));
}

void HardwareDebugSession::removeMdbWatch(const QString& expression)
{
    m_mdbWatches.removeAll(expression);
    m_mdbVariables.remove(expression);
    publishMdbVariables();
    emit mdbVariablesChanged();
}

void HardwareDebugSession::evaluateMdbExpression(
    const QString& expression,
    std::function<void(const QString&, const QString&)> callback)
{
    const QString trimmed = expression.trimmed();
    if (!callback || trimmed.isEmpty() || !m_mdbProcess || !m_mdbProcess->isRunning()) {
        if (callback)
            callback({}, {});
        return;
    }
    m_mdbEvaluations[trimmed].append(std::move(callback));
    m_mdbProcess->sendCommand(QStringLiteral("print %1").arg(trimmed));
}

void HardwareDebugSession::setMdbVariable(const QString& expression, const QString& value)
{
    const QString trimmed = expression.trimmed();
    if (!m_mdbProcess || !m_mdbProcess->isRunning() || trimmed.isEmpty())
        return;
    // Resolve from the ELF symbol table. XC16 MDB's `print /a` and symbolic
    // write both fail on some dsPIC releases with a trailing-space parse bug.
    const QString address = resolveMdbSymbolAddress(trimmed);
    if (address.isEmpty()) {
        emit debugOutput(QStringLiteral("[MDB] Cannot find symbol %1 in the ELF symbol table\n")
                         .arg(trimmed));
        return;
    }
    const QString command = QStringLiteral("write /r 0x%1 %2")
        .arg(address, value.trimmed());
    m_pendingMdbWrites.insert(command, trimmed);
    m_mdbProcess->sendCommand(command);
}

QString HardwareDebugSession::resolveMdbSymbolAddress(const QString& expression)
{
    const auto cached = m_mdbSymbolAddresses.constFind(expression);
    if (cached != m_mdbSymbolAddresses.constEnd())
        return cached.value();

    QStringList candidates;
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("xc16-nm"));
    if (!fromPath.isEmpty())
        candidates << fromPath;
    QDir versions(QStringLiteral("/opt/microchip/xc16"));
    const QStringList versionDirs = versions.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                        QDir::Name | QDir::Reversed);
    for (const QString& version : versionDirs)
        candidates << versions.filePath(version + QStringLiteral("/bin/xc16-nm"));

    QString tool;
    for (const QString& candidate : std::as_const(candidates)) {
        if (QFileInfo(candidate).isExecutable()) {
            tool = candidate;
            break;
        }
    }
    if (tool.isEmpty() || m_config.programImage.isEmpty())
        return {};

    QProcess nm;
    nm.start(tool, {QStringLiteral("-n"), m_config.programImage});
    if (!nm.waitForStarted(3000) || !nm.waitForFinished(10000))
        return {};
    const QString output = QString::fromLocal8Bit(nm.readAllStandardOutput());
    const QString symbol = QRegularExpression::escape(expression);
    const QRegularExpression pattern(
        QStringLiteral("(?m)^([0-9a-fA-F]+)\\s+\\S\\s+_?%1\\s*$").arg(symbol));
    const auto match = pattern.match(output);
    if (!match.hasMatch())
        return {};
    QString address = match.captured(1);
    while (address.size() > 1 && address.startsWith(QLatin1Char('0')))
        address.remove(0, 1);
    m_mdbSymbolAddresses.insert(expression, address);
    return address;
}

void HardwareDebugSession::refreshMdbVariables()
{
    if (!m_mdbProcess || !m_mdbProcess->isRunning())
        return;
    m_mdbVariables.clear();
    m_mdbProcess->sendCommand(QStringLiteral("backtrace full"));
    for (const QString& expression : std::as_const(m_mdbWatches))
        m_mdbProcess->sendCommand(QStringLiteral("print %1").arg(expression));
}

void HardwareDebugSession::publishMdbVariables()
{
    if (m_gdbSession)
        m_gdbSession->replaceExternalVariables(m_mdbVariables);
}

bool HardwareDebugSession::isActive() const
{
    return m_sessionState != SessionState::Idle &&
           m_sessionState != SessionState::Error &&
           m_sessionState != SessionState::ConfigInvalid;
}

void HardwareDebugSession::runTarget()
{
    if (usesMdb()) { m_mdbProcess->sendCommand(QStringLiteral("run")); setSessionState(SessionState::Running); }
    else if (m_gdbSession) m_gdbSession->run();
}

void HardwareDebugSession::continueTarget()
{
    if (usesMdb()) { m_mdbProcess->sendCommand(QStringLiteral("continue")); setSessionState(SessionState::Running); }
    else if (m_gdbSession) m_gdbSession->continueExecution();
}

void HardwareDebugSession::haltTarget()
{
    if (usesMdb()) { m_mdbProcess->sendCommand(QStringLiteral("halt")); setSessionState(SessionState::TargetHalted); }
    else if (m_gdbSession) m_gdbSession->interruptExecution();
}

void HardwareDebugSession::stepIntoTarget()
{
    if (usesMdb()) m_mdbProcess->sendCommand(QStringLiteral("step"));
    else if (m_gdbSession) m_gdbSession->stepInto();
}

void HardwareDebugSession::stepOverTarget()
{
    if (usesMdb()) m_mdbProcess->sendCommand(QStringLiteral("next"));
    else if (m_gdbSession) m_gdbSession->stepOver();
}

void HardwareDebugSession::stepOutTarget()
{
    if (usesMdb()) emit debugOutput(QStringLiteral(
        "[MDB] Step Out is not exposed by the MDB command-line interface.\n"));
    else if (m_gdbSession) m_gdbSession->stepOut();
}

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
    m_mdbBreakpointIds.clear();
    m_mdbBreakpointLines.clear();
    m_mdbSymbolAddresses.clear();

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
    if (config.serverType == HardwareServerType::MplabMdb) {
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] MDB: %1\n")
                         .arg(config.serverExecutable));
    } else {
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Server: %1\n")
                         .arg(config.serverExecutable));
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] GDB: %1\n")
                         .arg(config.gdbExecutable));
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Target: %1:%2\n")
                         .arg(config.host).arg(config.port));
    }
    if (!config.programImage.isEmpty()) {
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Image: %1\n")
                         .arg(config.programImage));
    }

    if (config.serverType == HardwareServerType::MplabMdb) {
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Backend: MPLAB MDB / %1\n")
                         .arg(config.mplabTool));
        emit debugOutput(QStringLiteral("[HARDWARE DEBUG] Device: %1\n")
                         .arg(config.mplabDevice));
        setSessionState(SessionState::ServerStarting);
        m_mdbProcess->start(config);
        return;
    }

    // Connect GDB session signals
    connect(m_gdbSession, &DebuggerSession::targetStarted,
            this, &HardwareDebugSession::onGdbTargetStarted, Qt::UniqueConnection);
    connect(m_gdbSession, &DebuggerSession::targetStartFailed,
            this, &HardwareDebugSession::onGdbTargetStartFailed, Qt::UniqueConnection);
    connect(m_gdbSession, &DebuggerSession::targetStopped,
            this, &HardwareDebugSession::onGdbTargetStopped, Qt::UniqueConnection);
    connect(m_gdbSession, &DebuggerSession::targetExited,
            this, &HardwareDebugSession::onGdbTargetExited, Qt::UniqueConnection);

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
        disconnect(m_gdbSession, &DebuggerSession::targetStartFailed,
                   this, &HardwareDebugSession::onGdbTargetStartFailed);
        disconnect(m_gdbSession, &DebuggerSession::targetStopped,
                   this, &HardwareDebugSession::onGdbTargetStopped);
        disconnect(m_gdbSession, &DebuggerSession::targetExited,
                   this, &HardwareDebugSession::onGdbTargetExited);
    }

    // Stop GDB first
    if (m_gdbSession && m_gdbSession->isRunning()) {
        m_gdbSession->terminateSession();
    }

    // Stop server
    if (m_mdbProcess && m_mdbProcess->isRunning())
        m_mdbProcess->stop();

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
    m_gdbSession->setRemoteConnectCommands(
        m_config.preConnectCommands,
        m_config.serverType == HardwareServerType::STLink);

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
    executeGdbSequence();
}

void HardwareDebugSession::onGdbTargetStartFailed(const QString& message)
{
    abortSession(message);
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
    // Symbol loading, pre-connect commands and target-select are performed by
    // DebuggerSession. This sequence starts once the remote target is connected.

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

            if (m_config.serverType == HardwareServerType::JLink ||
                m_config.serverType == HardwareServerType::STLink) {
                // ST-LINK and J-Link accept reset and halt as separate monitor
                // commands. "reset halt" is OpenOCD syntax.
                sendCommand(
                    QStringLiteral("-interpreter-exec console \"monitor reset\""));
                // Halt will be handled separately below
            } else {
                // Preserve the conventional combined command for generic
                // OpenOCD-style server profiles.
                sendCommand(
                    QStringLiteral("-interpreter-exec console \"monitor reset halt\""));
            }
        });
    }

    if (m_config.haltAfterReset &&
        (m_config.serverType == HardwareServerType::JLink ||
         m_config.serverType == HardwareServerType::STLink)) {
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

    if (m_config.serverType == HardwareServerType::STLink) {
        m_sequenceSteps.append([this](const QString&) {
            // STM32F4 independent/window watchdogs keep running while the core
            // is halted unless DBGMCU explicitly freezes them. Without this,
            // slow source stepping can reset the MCU and leave GDB waiting for
            // a temporary step breakpoint that can no longer be reached.
            emit debugOutput(QStringLiteral(
                "[HARDWARE DEBUG] Freezing STM32 watchdogs while halted\n"));
            sendCommand(QStringLiteral(
                "-interpreter-exec console \"set *(unsigned int *)0xE0042008 = "
                "*(unsigned int *)0xE0042008 | 0x00001800\""));
        });
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
    m_gdbSession->sendRawCommand(cmd, [this, cmd, cb = std::move(cb)](const QString& reply) {
        if (reply.contains(QStringLiteral("^error"))) {
            abortSession(QStringLiteral("GDB command failed: %1\n%2")
                         .arg(cmd, reply.trimmed()));
            return;
        }
        if (cb)
            cb(reply);
        advanceSequence(reply);
    });
}

void HardwareDebugSession::advanceSequence(const QString& reply)
{
    ++m_sequenceStep;
    if (m_sequenceStep < m_sequenceSteps.size())
        m_sequenceSteps[m_sequenceStep](reply);
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
