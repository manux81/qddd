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

#include "DebugSession.h"

#include <QFileInfo>
#include <QDebug>

// ======================
// Small utilities
// ======================

static QString decodeCString(QString s)
{
    if (s.startsWith('"') && s.endsWith('"'))
        s = s.mid(1, s.size() - 2);

    s.replace("\\n", "\n");
    s.replace("\\t", "\t");
    s.replace("\\\"", "\"");
    s.replace("\\r", "\r");
    s.replace("\\\\", "\\");
    return s;
}

static QString extractHexAddress(const QString& s)
{
	QRegularExpression re(R"(0x[0-9a-fA-F]+)");
	auto m = re.match(s);
	return m.hasMatch() ? m.captured(0) : QString{};
}


static bool looksLikePointer(const QString& v)
{
    const QString s = v.trimmed().toLower();
    return s.startsWith("0x") && s.size() > 2;
}

static bool looksLikeStruct(const QString& v)
{
	const QString s = v.trimmed();

	// struct / class / array / STL → { ... }
	if (s.startsWith("{") && s.endsWith("}"))
		return true;

	// vector / map empty: {}
	if (s == "{}")
		return true;

	return false;
}

static QString miGet(const QString& blob, const QString& key)
{
    QRegularExpression re(
        QRegularExpression::escape(key) + R"(="((?:\\.|[^"])*)"")"
    );
    auto m = re.match(blob);
    if (!m.hasMatch())
        return {};
    return decodeCString('"' + m.captured(1) + '"');
}

static QString miQuote(const QString& s)
{
	QString out = s;
	out.replace("\\", "\\\\");
	out.replace("\"", "\\\"");
	return QString("\"%1\"").arg(out);
}

static QString bpBestLocation(const BreakpointInfo& bp)
{
	if (!bp.originalLocation.isEmpty())
		return bp.originalLocation;
	if (!bp.file.isEmpty() && bp.line > 0)
		return QString("%1:%2").arg(bp.file).arg(bp.line);
	if (!bp.function.isEmpty())
		return bp.function;
	if (!bp.address.isEmpty())
		return bp.address;
	return QString();
}

// Extract top-level { ... } blocks (without outer braces)
static QVector<QString> miExtractBraceObjects(const QString& s)
{
    QVector<QString> out;
    int depth = 0;
    int start = -1;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (c == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && start >= 0) {
                out.push_back(s.mid(start + 1, i - start - 1));
                start = -1;
            }
        }
    }
    return out;
}

static QString formatDisassemblyFromReply(const QString& replyBlob)
{
	const int idx = replyBlob.indexOf("asm_insns=[");
	if (idx < 0)
		return replyBlob.trimmed();

	const QVector<QString> insns = miExtractBraceObjects(replyBlob.mid(idx));
	if (insns.isEmpty())
		return replyBlob.trimmed();

	QStringList lines;
	lines.reserve(insns.size());
	for (const auto& iblob : insns) {
		const QString addr = miGet(iblob, "address");
		const QString inst = miGet(iblob, "inst");
		QString opcode = miGet(iblob, "opcode");
		if (opcode.isEmpty())
			opcode = miGet(iblob, "opcodes");
		const QString fn = miGet(iblob, "func-name");
		const QString off = miGet(iblob, "offset");

		QString left = addr;
		if (!fn.isEmpty()) {
			left += " <" + fn;
			if (!off.isEmpty())
				left += "+" + off;
			left += ">";
		}

		opcode = opcode.simplified();

		QString line = left;
		if (!opcode.isEmpty())
			line += "\t" + opcode;
		if (!inst.isEmpty())
			line += "\t" + inst;

		if (!line.isEmpty())
			lines << line;
	}

	return lines.join('\n');
}

// Parse optional leading token: 12^done,...
static int parseLeadingToken(const QString& line, int* outPosAfterToken)
{
    int i = 0;
    while (i < line.size() && line[i].isDigit()) ++i;
    if (i == 0) return 0;
    bool ok = false;
    int tok = line.left(i).toInt(&ok);
    if (!ok) return 0;
    if (outPosAfterToken) *outPosAfterToken = i;
    return tok;
}

static QStringList splitTopLevelCommas(const QString& s)
{
	QStringList out;
	int depth = 0;
	int start = 0;

	for (int i = 0; i < s.size(); ++i) {
		QChar c = s[i];
		if (c == '{') depth++;
		else if (c == '}') depth--;
		else if (c == ',' && depth == 0) {
			out << s.mid(start, i - start).trimmed();
			start = i + 1;
		}
	}

	out << s.mid(start).trimmed();
	return out;
}

static void expandInlineStructIntoChildren(
	DebugVariable* node,
	const QString& value,
	int depth,
	int maxDepth)
{
	if (!node) return;
	if (depth >= maxDepth) return;

	QString v = value.trimmed();
	if (!looksLikeStruct(v))
		return;

	// strip outer { }
	v = v.mid(1, v.size() - 2).trimmed();
	if (v.isEmpty())
		return;

	const QStringList entries = splitTopLevelCommas(v);

	for (const QString& e : entries) {
		const int eq = e.indexOf('=');
		if (eq <= 0)
			continue;

		auto c = std::make_unique<DebugVariable>();
		c->name  = e.left(eq).trimmed();
		c->value = e.mid(eq + 1).trimmed();
		c->parent = node;

		c->isPointer   = looksLikePointer(c->value);
		c->hasChildren = looksLikeStruct(c->value);

		// ricorsione SOLO su struct inline
		if (c->hasChildren) {
			expandInlineStructIntoChildren(
				c.get(), c->value, depth + 1, maxDepth);
		}

		node->children.push_back(std::move(c));
	}

	if (!node->children.empty())
		node->hasChildren = true;
}


// ============================================================================
// DebugVariable
// ============================================================================

QString DebugVariable::fullPath() const
{
    if (!parent) return name;
    return parent->fullPath() + "." + name;
}

// ============================================================================
// DebuggerSession
// ============================================================================

DebuggerSession::DebuggerSession(QObject* parent)
    : QObject(parent)
{
    connect(&m_debuggerProcess,
            &QProcess::readyReadStandardOutput,
            this,
            &DebuggerSession::onDebuggerOutputReady);

    connect(&m_debuggerProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &DebuggerSession::onDebuggerFinished);
}

DebuggerSession::~DebuggerSession() = default;

void DebuggerSession::setBackend(Backend backend)
{
    m_backend = backend;
}

void DebuggerSession::setGdbExecutable(const QString& path)
{
	if (!path.trimmed().isEmpty())
		m_gdbExecutable = path.trimmed();
}

void DebuggerSession::setLldbMiExecutable(const QString& path)
{
	if (!path.trimmed().isEmpty())
		m_lldbMiExecutable = path.trimmed();
}

void DebuggerSession::setTargetType(TargetType type)
{
	m_targetType = type;
}

void DebuggerSession::setReverseMode(ReverseMode mode)
{
	m_reverseMode = mode;
	emit reverseExecutionAvailabilityChanged();
}

void DebuggerSession::setRemoteEndpoint(const QString& host, int port)
{
	const QString h = host.trimmed();
	if (!h.isEmpty())
		m_remoteHost = h;
	if (port > 0 && port <= 65535)
		m_remotePort = port;
}

void DebuggerSession::setRemoteConnectCommands(const QStringList& commands,
                                               bool extendedRemote)
{
	m_remoteConnectCommands = commands;
	m_useExtendedRemote = extendedRemote;
}

void DebuggerSession::setStlinkServerPath(const QString& path)
{
	if (!path.trimmed().isEmpty())
		m_stlinkServerPath = path.trimmed();
}

void DebuggerSession::setStlinkGdbPort(int port)
{
	if (port > 0 && port <= 65535)
		m_stlinkGdbPort = port;
}

bool DebuggerSession::isRemoteTarget() const
{
	return m_targetType == TargetType::RemoteGdbserver
		|| m_targetType == TargetType::JLink
		|| m_targetType == TargetType::Stlink;
}

QString DebuggerSession::remoteSpec() const
{
	return QString("%1:%2").arg(m_remoteHost).arg(m_remotePort);
}

void DebuggerSession::startSession(const QString& executablePath)
{
	m_reverseRecordingRequested = false;
	m_reverseRecordingFailed = false;
	m_reverseRecordingReady = false;
	emit reverseExecutionAvailabilityChanged();
    QFileInfo fi(executablePath);
    if (!fi.exists()) {
        qWarning() << "Executable not found:" << executablePath;
        return;
    }

	// For ST-Link, first launch the GDB server
	if (m_targetType == TargetType::Stlink) {
		QStringList serverArgs;
		serverArgs << "-g" << QString("-p%1").arg(m_stlinkGdbPort)
		           << "-l" << "-1"   // listen on localhost only via -g
		           << "-v";          // verbose

		emit debuggerOutput(tr("Starting ST-LINK GDB server: %1 %2\n")
		                      .arg(m_stlinkServerPath, serverArgs.join(' ')));

		m_stlinkProcess.start(m_stlinkServerPath, serverArgs);

		if (!m_stlinkProcess.waitForStarted(5000)) {
			qWarning() << "Failed to start ST-LINK GDB server:" << m_stlinkServerPath;
			emit debuggerOutput(tr("ERROR: Failed to start ST-LINK GDB server: %1\n")
			                      .arg(m_stlinkServerPath));
			return;
		}

		// Give the ST-Link server a moment to initialise
		m_stlinkProcess.waitForReadyRead(2000);
		const QByteArray stlinkOutput = m_stlinkProcess.readAllStandardOutput();
		if (!stlinkOutput.isEmpty())
			emit debuggerOutput(QString::fromUtf8(stlinkOutput));
	}

    QString debugger;
    QStringList args;

    switch (m_backend) {
    case Backend::GdbMi:
        debugger = m_gdbExecutable;
        args << "--interpreter=mi2";
        break;
    case Backend::LldbMi:
        debugger = m_lldbMiExecutable;
        args << "--interpreter=mi3";
        break;
    }

    m_debuggerProcess.start(debugger, args);

    if (!m_debuggerProcess.waitForStarted()) {
        qWarning() << "Failed to start debugger:" << debugger;
        return;
    }

	// 1) Load local symbols
	enqueueCommand(QString("-file-exec-and-symbols \"%1\"").arg(executablePath),
	               [this](const QString&) {
		               // 2) Optionally connect to a remote target
		               if (m_backend == Backend::GdbMi && isRemoteTarget()) {
						   // For ST-Link we connect to localhost:stlinkGdbPort
						   const QString host = (m_targetType == TargetType::Stlink)
							   ? QString("localhost")
							   : m_remoteHost;
						   const int port = (m_targetType == TargetType::Stlink)
							   ? m_stlinkGdbPort
							   : m_remotePort;

			               for (const QString& command : m_remoteConnectCommands) {
				               if (!command.trimmed().isEmpty())
					               enqueueCommand(command.trimmed());
			               }
			               const QString mode = m_useExtendedRemote
				               ? QStringLiteral("extended-remote")
				               : QStringLiteral("remote");
			               enqueueCommand(QString("-target-select %1 %2:%3").arg(mode, host).arg(port),
			                              [this](const QString&) { emit targetStarted(); });
			               return;
		               }

		               if (m_backend == Backend::GdbMi
		                   && m_reverseMode == ReverseMode::FullRecord) {
			               // GDB 12's full recorder cannot decode AVX instructions
			               // selected by glibc on modern x86_64 processors. Disabling
			               // tcache also avoids its getrandom syscall, which record
			               // full in GDB 12 cannot replay. Keep cpu.hwcaps last: glibc
			               // 2.35 does not reliably stop parsing it at the next ':'.
			               enqueueCommand(
			                   "-gdb-set environment GLIBC_TUNABLES=glibc.malloc.tcache_count=0:glibc.cpu.hwcaps=-AVX_Usable,-AVX2_Usable,-AVX512F_Usable,-AVX_Fast_Unaligned_Load");
		               }
		               emit targetStarted();
	               });
}

void DebuggerSession::terminateSession()
{
    if (m_debuggerProcess.state() != QProcess::NotRunning)
        m_debuggerProcess.kill();

    if (m_stlinkProcess.state() != QProcess::NotRunning)
        m_stlinkProcess.kill();
}

bool DebuggerSession::isRunning() const
{
    return m_debuggerProcess.state() != QProcess::NotRunning;
}

// ============================================================================
// Execution control
// ============================================================================

void DebuggerSession::run()
{
	// For remote targets, "-exec-run" is often unsupported/undesired; continue instead.
	if (m_backend == Backend::GdbMi && isRemoteTarget())
		enqueueCommand("-exec-continue");
	else
		enqueueCommand("-exec-run");
}
void DebuggerSession::continueExecution()   { enqueueCommand("-exec-continue"); }
void DebuggerSession::stepInto()            { enqueueCommand("-exec-step"); }
void DebuggerSession::stepOver()            { enqueueCommand("-exec-next"); }
void DebuggerSession::stepOut()             { enqueueCommand("-exec-finish"); }
void DebuggerSession::interruptExecution()  { enqueueCommand("-exec-interrupt"); }
void DebuggerSession::reverseContinueExecution() { executeReverseCommand("-exec-continue --reverse"); }
void DebuggerSession::reverseStepInto()          { executeReverseCommand("-exec-step --reverse"); }
void DebuggerSession::reverseStepOver()          { executeReverseCommand("-exec-next --reverse"); }

void DebuggerSession::executeReverseCommand(const QString& command)
{
	if (!supportsReverseExecution() || command.isEmpty())
		return;

	enqueueCommand(command);
}

void DebuggerSession::ensureReverseRecording()
{
	if (m_backend != Backend::GdbMi || m_reverseMode == ReverseMode::Disabled
	    || m_reverseRecordingRequested || m_reverseRecordingFailed)
		return;

	m_reverseRecordingRequested = true;
	const bool fullRecord = m_reverseMode == ReverseMode::FullRecord;
	if (fullRecord) {
		emit debuggerOutput(tr(
		    "Starting experimental GDB full recording. Library calls, syscalls or unsupported instructions may stop recording.\n"));
	}

	const QString recordCommand = fullRecord
		? QStringLiteral("-interpreter-exec console \"record full\"")
		: QStringLiteral("-interpreter-exec console \"record btrace\"");
	enqueueCommand(recordCommand, [this, fullRecord](const QString& reply) {
		if (reply.contains("^done")) {
			m_reverseRecordingReady = true;
			emit reverseExecutionAvailabilityChanged();
			return;
		}

		m_reverseRecordingRequested = false;
		m_reverseRecordingFailed = true;
		m_reverseRecordingReady = false;
		if (!fullRecord) {
			emit debuggerOutput(tr(
			    "Reverse execution is unavailable: GDB could not enable branch tracing. "
			    "On Linux, set kernel.perf_event_paranoid to 2 or less. Normal debugging remains enabled.\n"));
		} else {
			emit debuggerOutput(tr(
			    "GDB full recording could not be started. Normal debugging remains enabled.\n"));
		}
		emit reverseExecutionAvailabilityChanged();
	});
}

bool DebuggerSession::supportsReverseExecution() const
{
	// Conservative: GDB MI supports reverse-* when process recording is enabled.
	// LLDB MI support varies; keep disabled by default.
	return m_backend == Backend::GdbMi && m_reverseRecordingReady
		&& !m_reverseRecordingFailed;
}
void DebuggerSession::runToCursor(const QString& location)
{
	if (location.isEmpty())
		return;

	enqueueCommand(
		QString("-break-insert -t %1").arg(location),
		[this](const QString& reply) {

			// 1) estrai bkpt={...}
			const int bkptPos = reply.indexOf("bkpt={");
			if (bkptPos < 0) {
				// fallback: continua comunque
				enqueueCommand("-exec-continue");
				return;
			}

			const int bracePos = reply.indexOf('{', bkptPos);
			if (bracePos < 0) {
				enqueueCommand("-exec-continue");
				return;
			}

			int depth = 0;
			int end = -1;
			for (int i = bracePos; i < reply.size(); ++i) {
				if (reply[i] == '{') ++depth;
				else if (reply[i] == '}') {
					--depth;
					if (depth == 0) { end = i; break; }
				}
			}

			if (end <= bracePos) {
				enqueueCommand("-exec-continue");
				return;
			}

			const QString bkptBlob = reply.mid(bracePos + 1, end - bracePos - 1);

			// 2) id (GDB=number, LLDB=id)
			QString idStr = miGet(bkptBlob, "number");
			if (idStr.isEmpty())
				idStr = miGet(bkptBlob, "id");

			bool ok = false;
			const int bpId = idStr.toInt(&ok);

			// 3) continua
			enqueueCommand("-exec-continue");

			// 4) sicurezza extra: delete esplicito (opzionale)
			if (ok && bpId > 0) {
				removeBreakpoint(bpId);
			}
		}
	);
}



// ============================================================================
// Breakpoints
// ============================================================================

void DebuggerSession::insertBreakpoint(const QString& location)
{
    enqueueCommand(QString("-break-insert %1").arg(location),
        [this](const QString& reply) {
			handleBreakpointEvent(reply);
        });
}

void DebuggerSession::removeBreakpoint(int breakpointId)
{
	bool removed = false;
	for (int i = 0; i < m_breakpoints.size(); ) {
		if (m_breakpoints[i].number == breakpointId) {
			m_breakpoints.removeAt(i);
			removed = true;
		} else {
			++i;
		}
	}


	if (!removed)
		return;

	emit breakpointsUpdated();

	enqueueCommand(QString("-break-delete %1").arg(breakpointId),
		[](const QString&) {
			// Nothing here!
		});
}


void DebuggerSession::clearAllBreakpoints()
{
    enqueueCommand("-break-delete",
        [this](const QString&) {
            emit breakpointsUpdated();
        });
}

void DebuggerSession::setBreakpointEnabled(int breakpointId, bool enabled)
{
    enqueueCommand(QString("-break-%1 %2")
                       .arg(enabled ? "enable" : "disable")
                       .arg(breakpointId),
        [this](const QString&) {
            emit breakpointsUpdated();
        });
}

void DebuggerSession::toggleBreakpoint(const QString& location)
{
	int line = -1;
	QString file;
	QString func;

	const int separator = location.lastIndexOf(':');
	if (separator < 0) {
		func = location;
	} else {
		file = location.left(separator);
		line = location.mid(separator + 1).toInt();
	}

	auto it = std::find_if(
		m_breakpoints.begin(),
		m_breakpoints.end(),
		[&](const BreakpointInfo& bp) {
			if (!func.isEmpty())
				return bp.function == func;
			if (bp.line != line)
				return false;
			const QFileInfo requested(file);
			const QFileInfo existing(bp.file);
			const QString requestedPath = requested.canonicalFilePath().isEmpty()
				? requested.absoluteFilePath() : requested.canonicalFilePath();
			const QString existingPath = existing.canonicalFilePath().isEmpty()
				? existing.absoluteFilePath() : existing.canonicalFilePath();
			return requestedPath == existingPath;
		}
	);

	if (it == m_breakpoints.end()) {
		insertBreakpoint(location);
	} else {
		removeBreakpoint(it->number);
	}
}

void DebuggerSession::updateBreakpointCondition(int breakpointId, const QString& expr)
{
	const QString e = expr.trimmed();

	if (m_backend == Backend::GdbMi) {
		enqueueCommand(QString("-break-condition %1 %2").arg(breakpointId).arg(miQuote(e)));
		return;
	}

	// LLDB console best-effort (works when using LLDB as the underlying debugger).
	if (e.isEmpty()) {
		enqueueCommand(QString("-interpreter-exec console \"breakpoint modify -c '' %1\"")
		                   .arg(breakpointId));
	} else {
		enqueueCommand(QString("-interpreter-exec console \"breakpoint modify -c %1 %2\"")
		                   .arg(miQuote(e))
		                   .arg(breakpointId));
	}
}

void DebuggerSession::updateBreakpointIgnoreCount(int breakpointId, int ignoreCount)
{
	const int v = qMax(0, ignoreCount);

	if (m_backend == Backend::GdbMi) {
		enqueueCommand(QString("-break-after %1 %2").arg(breakpointId).arg(v));
		return;
	}

	enqueueCommand(QString("-interpreter-exec console \"breakpoint modify -i %1 %2\"")
	                   .arg(v)
	                   .arg(breakpointId));
}

void DebuggerSession::updateBreakpointTemporary(int breakpointId, bool temporary)
{
	int idx = -1;
	for (int i = 0; i < m_breakpoints.size(); ++i) {
		if (m_breakpoints[i].number == breakpointId) {
			idx = i;
			break;
		}
	}
	if (idx < 0)
		return;

	const BreakpointInfo bp = m_breakpoints[idx];
	if (bp.temporary == temporary)
		return;

	const QString location = bpBestLocation(bp);
	if (location.isEmpty())
		return;

	// Recreate as temp/non-temp.
	removeBreakpoint(breakpointId);

	if (m_backend == Backend::GdbMi) {
		enqueueCommand(QString("-break-insert %1 %2")
		                   .arg(temporary ? "-t" : "")
		                   .arg(location));
	} else {
		// LLDB console best-effort one-shot.
		enqueueCommand(QString("-interpreter-exec console \"breakpoint set %1 %2\"")
		                   .arg(temporary ? "-o true" : "-o false")
		                   .arg(location));
	}
}

void DebuggerSession::requestDisassembly(const QString& file, int line, int instructionCount)
{
	if (file.trimmed().isEmpty() || line <= 0 || instructionCount <= 0)
		return;

	const QString header = QString("%1:%2\n").arg(file).arg(line);

	// GDB/MI supports -data-disassemble with -f/-l/-n; LLDB/MI usually doesn't.
	if (m_backend == Backend::GdbMi) {
		// Prefer address-based disassembly when we have the current PC; it tends to align
		// better with highlighting.
		const QString cmd = !m_lastStopAddr.isEmpty()
			? QString("-data-disassemble -a %1 -n %2 -- 0")
			      .arg(m_lastStopAddr)
			      .arg(instructionCount)
			: QString("-data-disassemble -f \"%1\" -l %2 -n %3 -- 0")
			      .arg(file)
			      .arg(line)
			      .arg(instructionCount);

		enqueueCommand(cmd, [this, header](const QString& reply) {
			const QString formatted = formatDisassemblyFromReply(reply);
			emit disassemblyUpdated(header + formatted);
		});
		return;
	}

	// LLDB console: disassemble current frame and capture stream output.
	m_captureDisassembly = true;
	m_disassemblyBuffer.clear();

	// Prefer address-based disassembly when possible.
	const QString cmd = !m_lastStopAddr.isEmpty()
		? QString("-interpreter-exec console \"disassemble -s %1 -c %2\"")
		      .arg(m_lastStopAddr)
		      .arg(instructionCount)
		: QString("-interpreter-exec console \"disassemble -f -c %1\"")
		      .arg(instructionCount);
	enqueueCommand(cmd, [this, header](const QString&) {
		m_captureDisassembly = false;
		const QString body = m_disassemblyBuffer.trimmed();
		m_disassemblyBuffer.clear();
		emit disassemblyUpdated(header + (body.isEmpty() ? QStringLiteral("(no disassembly output)") : body));
	});
}

void DebuggerSession::requestDisassemblyAtLastStop(int instructionCount)
{
	if (m_lastStopFile.isEmpty() || m_lastStopLine <= 0)
		return;
	requestDisassembly(m_lastStopFile, m_lastStopLine, instructionCount);
}


const QVector<BreakpointInfo>& DebuggerSession::breakpoints() const
{
	return m_breakpoints;
}

// ============================================================================
// Stack nav
// ============================================================================

void DebuggerSession::selectStackFrame(int frameIndex)
{
    enqueueCommand(QString("-stack-select-frame %1").arg(frameIndex),
        [this](const QString&) {
            requestStopState();
        });
}

// ============================================================================
// Command queue (tokened)
// ============================================================================

void DebuggerSession::enqueueCommand(const QString& command,
                                    std::function<void(const QString&)> cb)
{
    PendingCommand pc;
    pc.token = m_nextToken++;
    pc.command = command;
    pc.cb = std::move(cb);
    m_commandQueue.enqueue(std::move(pc));
    processCommandQueue();
}

void DebuggerSession::processCommandQueue()
{
    if (m_commandInFlight || m_commandQueue.isEmpty())
        return;

    m_commandInFlight = true;
    m_inFlight = m_commandQueue.dequeue();
    m_inFlightReply.clear();

    const QString wire = QString::number(m_inFlight.token) + m_inFlight.command;

    qDebug().noquote() << "[MI SEND]" << wire;
    m_debuggerProcess.write((wire + "\n").toUtf8());
}

void DebuggerSession::onDebuggerOutputReady()
{
    const QByteArray raw = m_debuggerProcess.readAllStandardOutput();
    const QList<QByteArray> lines = raw.split('\n');

    for (const QByteArray& l : lines) {
        const QString line = QString::fromUtf8(l).trimmed();
        if (!line.isEmpty())
            dispatchDebuggerMessage(line);
    }
}

void DebuggerSession::onDebuggerFinished(int exitCode,
                                         QProcess::ExitStatus)
{
    emit targetExited(exitCode);
}

// ============================================================================
// Dispatcher
// ============================================================================
void DebuggerSession::dispatchDebuggerMessage(const QString& rawLine)
{
	QString line = rawLine.trimmed();
	if (line.isEmpty())
		return;

	// ------------------------------------------------------------
	// 0) Ignore debugger prompt
	// ------------------------------------------------------------
	if (line == "(gdb)" || line == "(lldb)" || line == ">")
		return;

	qDebug().noquote() << "[MI RECV]" << rawLine;
	// ------------------------------------------------------------
	// 1) Strip MI token (if present)
	//    e.g. "3^running" -> tok=3, line="^running"
	// ------------------------------------------------------------
	int posAfterTok = 0;
	const int tok = parseLeadingToken(line, &posAfterTok);
	if (tok > 0 && posAfterTok < line.size())
		line = line.mid(posAfterTok);

	// ------------------------------------------------------------
	// 2) Stream records (console / target / log)
	// ------------------------------------------------------------
	if (line.startsWith("~\"") || line.startsWith("@\"") || line.startsWith("&\"")) {
		const QString decoded = decodeCString(line.mid(2));
		if (decoded.contains("Process record: failed to record execution log")) {
			m_reverseRecordingRequested = false;
			m_reverseRecordingFailed = true;
			m_reverseRecordingReady = false;
			enqueueCommand("-interpreter-exec console \"record stop\"");
			emit debuggerOutput(tr(
			    "Reverse recording failed in this function and was stopped. Reverse execution is disabled for this session; normal debugging remains enabled.\n"));
			emit reverseExecutionAvailabilityChanged();
		}
		if (m_captureDisassembly)
			m_disassemblyBuffer += decoded;
		emit debuggerOutput(decoded);
		return;
	}

	// ------------------------------------------------------------
	// 3) Async exec state
	// ------------------------------------------------------------
	if (line.startsWith("*stopped")) {
		onTargetStoppedInternal(line);
		return;
	}

	if (line.startsWith("*running")) {
		emit targetRunning();
		return;
	}

	// ------------------------------------------------------------
	// 4) Async notifications (=...)
	// ------------------------------------------------------------
	if (line.startsWith("=")) {

		if (line.startsWith("=library-"))
			return;

		if (line.startsWith("=thread-"))
			return;

		if (line.startsWith("=breakpoint-created") ||
			line.startsWith("=breakpoint-modified")) {
			handleBreakpointEvent(line);
			return;
		}

		if (line.startsWith("=breakpoint-deleted")) {
			handleBreakpointDeleted(line);
			return;
		}

		// fallback: debug-only
		qDebug().noquote() << "[MI NOTIFY]" << line;
		return;
	}


	// ------------------------------------------------------------
	// 5) Result record (^done, ^running, ^error, ...)
	// ------------------------------------------------------------
	if (line.startsWith("^")) {
		handleResultRecord(tok, line);
		return;
	}

	// ------------------------------------------------------------
	// 6) Fallback raw output (warnings, misc, LLDB quirks)
	// ------------------------------------------------------------
	emit debuggerOutput(line);
}


void DebuggerSession::handleResultRecord(int token, const QString& resultLine)
{
	// Accumula solo se è la risposta del comando in flight
	if (m_commandInFlight && token == m_inFlight.token) {

		m_inFlightReply += QString::number(token) + resultLine + "\n";


		if (resultLine.startsWith("^done") ||
			resultLine.startsWith("^error") ||
			resultLine.startsWith("^running")) {

			// esegui callback del comando (se presente)
			if (m_inFlight.cb)
				m_inFlight.cb(m_inFlightReply);

			// sblocca e manda prossimo comando
			m_commandInFlight = false;
			processCommandQueue();
		}

		return;
	}

	// Token inatteso (non dovrebbe capitare, ma logghiamo)
	qDebug().noquote()
		<< "[MI WARN] token mismatch:"
		<< "got=" << token
		<< "expected=" << (m_commandInFlight ? m_inFlight.token : -1)
		<< "line=" << resultLine;
}


// ============================================================================
// Stop handling
// ============================================================================

void DebuggerSession::onTargetStoppedInternal(const QString& stopMsg)
{
    ++m_stepCounter;

    // frame={...} dentro *stopped,...
    const int framePos = stopMsg.indexOf("frame={");
    if (framePos >= 0) {
        const int bracePos = stopMsg.indexOf('{', framePos);
        if (bracePos >= 0) {
            int depth = 0;
            int end = -1;
            for (int i = bracePos; i < stopMsg.size(); ++i) {
                if (stopMsg[i] == '{') ++depth;
                else if (stopMsg[i] == '}') {
                    --depth;
                    if (depth == 0) { end = i; break; }
                }
            }
            if (end > bracePos) {
                const QString fblob = stopMsg.mid(bracePos + 1, end - bracePos - 1);

                const QString func = miGet(fblob, "func");
                const QString fullname = miGet(fblob, "fullname");
                const QString file = miGet(fblob, "file");
                const QString path = !fullname.isEmpty() ? fullname : file;
				const QString addr = miGet(fblob, "addr");

                bool ok = false;
                const int lineNo = miGet(fblob, "line").toInt(&ok);

				if (!path.isEmpty() && ok && lineNo > 0) {
					m_lastStopFile = path;
					m_lastStopLine = lineNo;
					m_lastStopFunction = func;
					emit stoppedAt(path, lineNo, func);
				}
				if (!addr.isEmpty()) {
					m_lastStopAddr = addr;
					emit stoppedAtAddress(addr);
				}
            }
        }
    }

	ensureReverseRecording();
    requestStopState();
    emit targetStopped();
}

// Metti questi metodi nel .cpp (e dichiarali nel .h come membri privati/protetti)

static int toIntSafe(const QString& s)
{
	bool ok = false;
	int v = s.toInt(&ok);
	return ok ? v : -1;
}

static bool parseEnabled(const QString& s)
{
	const QString v = s.trimmed().toLower();
	return (v == "y" || v == "yes" || v == "true" || v == "1");
}

void DebuggerSession::handleBreakpointEvent(const QString& line)
{
	// Example:
	// =breakpoint-created,bkpt={number="1",type="breakpoint",disp="keep",enabled="y",addr="0x...",func="main",file="x.cpp",fullname="/.../x.cpp",line="12"}
	// =breakpoint-modified,bkpt={...}

	const int bkptPos = line.indexOf("bkpt={");
	if (bkptPos < 0) {
		// Alcuni backend potrebbero mandare formati diversi.
		qDebug().noquote() << "[MI WARN] breakpoint event without bkpt={}: " << line;
		return;
	}

	const int bracePos = line.indexOf('{', bkptPos);
	if (bracePos < 0)
		return;

	// Trova la chiusura della graffa corrispondente (come fai nello stop handler)
	int depth = 0;
	int end = -1;
	for (int i = bracePos; i < line.size(); ++i) {
		if (line[i] == '{') ++depth;
		else if (line[i] == '}') {
			--depth;
			if (depth == 0) { end = i; break; }
		}
	}
	if (end <= bracePos)
		return;

	const QString bkptBlob = line.mid(bracePos + 1, end - bracePos - 1);

	// In GDB MI è "number", in altri può essere "id"
	QString idStr = miGet(bkptBlob, "number");
	if (idStr.isEmpty())
		idStr = miGet(bkptBlob, "id");

	const int id = toIntSafe(idStr);
	if (id <= 0) {
		qDebug().noquote() << "[MI WARN] breakpoint without valid id/number: " << line;
		return;
	}

	// Fields
	const QString enabledStr = miGet(bkptBlob, "enabled");
	const bool enabled = enabledStr.isEmpty() ? true : parseEnabled(enabledStr);

	const QString addr = miGet(bkptBlob, "addr");
	const QString func = miGet(bkptBlob, "func");
	const QString disp = miGet(bkptBlob, "disp");

	const QString fullname = miGet(bkptBlob, "fullname");
	const QString file = miGet(bkptBlob, "file");
	const QString path = !fullname.isEmpty() ? fullname : file;

	const int lineNo = toIntSafe(miGet(bkptBlob, "line"));

	const QString cond = miGet(bkptBlob, "cond");
	const int ignoreCount = toIntSafe(miGet(bkptBlob, "ignore"));
	// Some version: "times" or "hit-count"
	const QString timesStr = miGet(bkptBlob, "times");
	const int hitCount = toIntSafe(timesStr);
	const QString originalLoc = miGet(bkptBlob, "original-location");

	// 1) cerca se esiste già
	int idx = -1;
	for (int i = 0; i < m_breakpoints.size(); ++i) {
		if (m_breakpoints[i].number == id) { // <-- adatta il nome campo
			idx = i;
			break;
		}
	}

	BreakpointInfo info;
	info.number = id;
	info.enabled = enabled;
	info.address = addr;
	info.function = func;
	info.file = path;
	info.line = (lineNo > 0 ? lineNo : 0);
	info.condition = cond;
	info.ignoreCount = (ignoreCount >= 0 ? ignoreCount : 0);
	info.hitCount = (hitCount >= 0 ? hitCount : 0);
	info.temporary = (disp.trimmed().toLower() == "del");
	if (!originalLoc.isEmpty())
		info.originalLocation = originalLoc;

	if (idx >= 0) {
		// Preserve originalLocation if the backend doesn't report it.
		if (info.originalLocation.isEmpty())
			info.originalLocation = m_breakpoints[idx].originalLocation;
		m_breakpoints[idx] = info;
	} else {
		m_breakpoints.push_back(info);
	}

	emit breakpointsUpdated();
}

void DebuggerSession::handleBreakpointDeleted(const QString& line)
{
	// Example:
	// =breakpoint-deleted,id="1"
	// or in some cases bkpt={number="1"...}

	QString idStr = miGet(line, "id");
	if (idStr.isEmpty()) {
		// fallback: try to read bkpt={...} if is present
		const int bkptPos = line.indexOf("bkpt={");
		if (bkptPos >= 0) {
			const int bracePos = line.indexOf('{', bkptPos);
			if (bracePos >= 0) {
				int depth = 0;
				int end = -1;
				for (int i = bracePos; i < line.size(); ++i) {
					if (line[i] == '{') ++depth;
					else if (line[i] == '}') {
						--depth;
						if (depth == 0) { end = i; break; }
					}
				}
				if (end > bracePos) {
					const QString bkptBlob = line.mid(bracePos + 1, end - bracePos - 1);
					idStr = miGet(bkptBlob, "number");
					if (idStr.isEmpty())
						idStr = miGet(bkptBlob, "id");
				}
			}
		}
	}

	const int id = toIntSafe(idStr);
	if (id <= 0) {
		qDebug().noquote() << "[MI WARN] breakpoint-deleted without valid id: " << line;
		return;
	}

	for (int i = 0; i < m_breakpoints.size(); /*incremento dentro*/) {
		if (m_breakpoints[i].number == id) { // <-- adatta
			m_breakpoints.removeAt(i);
			continue;
		}
		++i;
	}

	emit breakpointsUpdated();
}


// ============================================================================
// State fetch
// ============================================================================

void DebuggerSession::requestStopState()
{
    m_pendingStack = true;
    m_pendingVariables = true;

    enqueueCommand("-stack-list-frames",
        [this](const QString& reply) {
            parseStackFromReply(reply);
            m_pendingStack = false;
            emit stackFramesUpdated();
            finalizeSnapshotIfReady();
        });

    enqueueCommand("-stack-select-frame 0");
    enqueueCommand("-stack-list-variables --all-values",
        [this](const QString& reply) {
            parseVarsFromReply(reply);
            m_pendingVariables = false;
            //emit variablesUpdated();
            finalizeSnapshotIfReady();
        });
}

void DebuggerSession::parseStackFromReply(const QString& replyBlob)
{
    m_stackFrames.clear();

    // cerca stack=[frame={...},frame={...}]
    int idx = replyBlob.indexOf("stack=[");
    if (idx < 0)
        return;

    const QVector<QString> frames = miExtractBraceObjects(replyBlob.mid(idx));

    for (const auto& fblob : frames) {
        StackFrame f;
        f.level    = miGet(fblob, "level");
        f.function = miGet(fblob, "func");

        const QString fullname = miGet(fblob, "fullname");
        const QString file     = miGet(fblob, "file");
        f.file = !fullname.isEmpty() ? fullname : file;

        bool ok = false;
        f.line = miGet(fblob, "line").toInt(&ok);
        if (!ok) f.line = 0;

        if (!f.function.isEmpty() || !f.file.isEmpty())
            m_stackFrames.push_back(f);
    }
}

void DebuggerSession::parseVarsFromReply(const QString& replyBlob)
{
    m_variables.clear();
	m_pendingPointerExpansions = 0;

    int idx = replyBlob.indexOf("variables=[");
    if (idx < 0)
        return;

    const QVector<QString> vars = miExtractBraceObjects(replyBlob.mid(idx));

    for (const auto& vblob : vars) {
        auto dv = std::make_unique<DebugVariable>();
        dv->name    = miGet(vblob, "name");
        dv->value   = miGet(vblob, "value");
        dv->type    = miGet(vblob, "type");

        dv->isPointer   = looksLikePointer(dv->value);
        dv->hasChildren = looksLikeStruct(dv->value);
        dv->parent      = nullptr;

		expandInlineStructIntoChildren(
			dv.get(),
			dv->value,
			0,
			2   // profondità massima (come prima)
		);

		if (!dv->name.isEmpty()) {
			DebugVariable* raw = dv.get();
			m_variables.push_back(std::move(dv));

			// If this is a pointer, try to dereference it and parse inline struct
			// fields (when available) so the UI can expand it.
			const bool looksNullPtr =
				raw->value.trimmed().toLower().startsWith("0x0") ||
				raw->value.trimmed().toLower() == "0x00000000";

			if (raw->isPointer && !looksNullPtr && raw->type.contains('*')) {
				++m_pendingPointerExpansions;
				enqueueCommand(
					QString("-data-evaluate-expression \"*%1\"").arg(raw->fullPath()),
					[this, raw](const QString& reply) {
						const QString deref = miGet(reply, "value").trimmed();
						if (!deref.isEmpty() && looksLikeStruct(deref)) {
							raw->children.clear();
							expandInlineStructIntoChildren(raw, deref, 0, 2);
						}

						if (--m_pendingPointerExpansions == 0 && m_pendingAddressRequests == 0)
							emit variablesUpdated();
					}
				);
			}

			++m_pendingAddressRequests;
			enqueueCommand(QString("-data-evaluate-expression \"&%1\"")
			                   .arg(raw->fullPath()),
			               [this, raw](const QString &reply) {
				               QString addr = extractHexAddress(reply);
				               if (!addr.isEmpty())
					               raw->address = addr;
								if (--m_pendingAddressRequests == 0) {
									if (m_pendingPointerExpansions == 0)
										emit variablesUpdated();
								}
			               });
		}
    }
}

// ============================================================================
// Snapshot
// ============================================================================

void DebuggerSession::finalizeSnapshotIfReady()
{
    if (m_pendingStack || m_pendingVariables)
        return;

    captureExecutionSnapshot();
}

void DebuggerSession::captureExecutionSnapshot()
{
    ExecutionSnapshot snapshot;
    snapshot.stepIndex = m_stepCounter;

    for (const auto& var : m_variables)
        snapshot.variableValues.insert(var->fullPath(), var->value);

    m_executionHistory.push_back(snapshot);

    if (m_executionHistory.size() >= 2) {
        computeVariableChanges(
            m_executionHistory[m_executionHistory.size() - 2],
            m_executionHistory.back()
        );
    }

    emit snapshotCaptured(snapshot);
	emit variablesUpdated();
}

void DebuggerSession::computeVariableChanges(const ExecutionSnapshot& previous,
                                            const ExecutionSnapshot& current)
{
    QVector<VariableChange> changes;
    m_changedPaths.clear();

    for (auto it = current.variableValues.begin();
         it != current.variableValues.end(); ++it) {

        const QString& path = it.key();
        const QString& newValue = it.value();

        if (!previous.variableValues.contains(path)) {
            changes.push_back({path, {}, newValue});
            m_changedPaths.insert(path);
        }
        else if (previous.variableValues[path] != newValue) {
            changes.push_back({path, previous.variableValues[path], newValue});
            m_changedPaths.insert(path);
        }
    }

    emit variableChangesDetected(changes, previous.stepIndex, current.stepIndex);
}

// ============================================================================
// Expression / raw
// ============================================================================

void DebuggerSession::evaluateExpression(const QString& expression)
{
    enqueueCommand(QString("-data-evaluate-expression \"%1\"").arg(expression),
        [this](const QString& reply) {
            emit debuggerOutput(reply);
        });
}

void DebuggerSession::sendRawCommand(const QString& cmd,
	                                  std::function<void(const QString&)> cb)
{
    enqueueCommand(cmd,
        [this, cb = std::move(cb)](const QString& reply) {
            emit debuggerOutput(reply);
            if (cb)
                cb(reply);
        });
}

void DebuggerSession::setVariable(const QString& fullPath,
								 const QString& newValue)
{
	if (fullPath.isEmpty())
		return;

	if (!isRunning())
		return;

	const QString expr =
		QString("%1 = %2").arg(fullPath, newValue);

	enqueueCommand(
		QString("-data-evaluate-expression \"%1\"").arg(expr),
		[this, fullPath, newValue](const QString& reply) {

			if (reply.contains("^error")) {
				qWarning().noquote()
					<< "[SET VAR FAILED]"
					<< fullPath << "=" << newValue
					<< "\n" << reply;
				return;
			}

			requestStopState();
		}
	);
}

void DebuggerSession::dereferencePointer(
	const QString& pointerExpr,
	std::function<void(const QString& value, const QString& type)> cb)
{
	if (pointerExpr.isEmpty() || !cb)
		return;

	if (!isRunning())
		return;

	enqueueCommand(
		QString("-data-evaluate-expression \"*%1\"").arg(pointerExpr),
		[this, cb = std::move(cb)](const QString& reply) mutable {
			if (reply.contains("^error")) {
				cb({}, {});
				return;
			}

			cb(miGet(reply, "value"), miGet(reply, "type"));
		}
	);
}

void DebuggerSession::evaluateExpressionValue(
	const QString& expr,
	std::function<void(const QString& value, const QString& type)> cb)
{
	if (expr.isEmpty() || !cb)
		return;

	if (!isRunning())
		return;

	enqueueCommand(
		QString("-data-evaluate-expression \"%1\"").arg(expr),
		[this, cb = std::move(cb)](const QString& reply) mutable {
			if (reply.contains("^error")) {
				cb({}, {});
				return;
			}

			cb(miGet(reply, "value"), miGet(reply, "type"));
		}
	);
}


// ============================================================================
// Accessors
// ============================================================================

const QVector<StackFrame>& DebuggerSession::stackFrames() const { return m_stackFrames; }
const std::vector<std::unique_ptr<DebugVariable>>& DebuggerSession::variables() const { return m_variables; }
const QVector<ExecutionSnapshot>& DebuggerSession::executionHistory() const { return m_executionHistory; }

const ExecutionSnapshot* DebuggerSession::snapshotAt(int index) const
{
    if (index < 0 || index >= m_executionHistory.size())
        return nullptr;
    return &m_executionHistory[index];
}

const QSet<QString>& DebuggerSession::changedPaths() const { return m_changedPaths; }
