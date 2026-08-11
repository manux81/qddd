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
#include <QProcess>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QString>
#include <QQueue>
#include <QRegularExpression>
#include <memory>
#include <vector>
#include <functional>

// ============================================================================
// Forward declarations
// ============================================================================

struct DebugVariable;
struct ExecutionSnapshot;
struct VariableChange;

struct BreakpointAction
{
	enum class Type {
		LogMessage,
		DebuggerCommand
	};

	Type type;
	QString payload;
};

struct BreakpointInfo
{
	// Identity
	int number = -1;
	QString type;

	// Position
	QString file;
	int line = 0;
	QString function;
	QString address;

	// State
	bool enabled = true;
	bool pending = false;
	bool temporary = false;

	// Condition / counter
	QString condition;
	int ignoreCount = 0; // times to ignore before stopping (MI: "ignore")
	int hitCount = 0;
	bool autoContinue = false;

	// UI / metadata
	QString name;
	QVector<BreakpointAction> actions;

	QString originalLocation;
};



struct StackFrame
{
	QString level;
	QString function;
	QString file;
	int line = 0;
};

// ============================================================================
// Debug variable tree
// ============================================================================

struct DebugVariable
{
	QString name;
	QString value;
	QString type;
	QString address;

	bool isPointer   = false;
	bool hasChildren = false;
	bool isWatch     = false;

	DebugVariable* parent = nullptr;
	std::vector<std::unique_ptr<DebugVariable>> children;

	[[nodiscard]]
	QString fullPath() const;
};

// ============================================================================
// Execution snapshot (time-travel unit)
// ============================================================================

struct ExecutionSnapshot
{
	int stepIndex = 0;
	qint64 timestampNs = 0;

	QString file;
	int line = 0;
	QString function;

	QHash<QString, QString> variableValues;
};

// ============================================================================
// Variable diff (causal-light event)
// ============================================================================

struct VariableChange
{
	QString path;
	QString oldValue;
	QString newValue;
};

// ============================================================================
// Debugger session controller
// ============================================================================

class DebuggerSession final : public QObject
{
	Q_OBJECT

public:
	enum class Backend {
		GdbMi,
		LldbMi
	};

	enum class TargetType {
		Local,
		RemoteGdbserver,
		JLink,
		Stlink
	};
	enum class ReverseMode {
		Disabled,
		Auto,
		BranchTrace,
		FullRecord
	};

	explicit DebuggerSession(QObject* parent = nullptr);
	~DebuggerSession() override;

	void setBackend(Backend backend);
	void setGdbExecutable(const QString& path);
	void setLldbMiExecutable(const QString& path);
	void setTargetType(TargetType type);
	void setReverseMode(ReverseMode mode);
	void setRemoteEndpoint(const QString& host, int port);
	void setRemoteConnectCommands(const QStringList& commands, bool extendedRemote = false);
	void setStlinkServerPath(const QString& path);
	void setStlinkGdbPort(int port);
	void startSession(const QString& executablePath);
	void terminateSession();

	[[nodiscard]] bool isRunning() const;

	// Execution control
	void run();
	void continueExecution();
	void stepInto();
	void stepOver();
	void stepOut();
	void interruptExecution();
	void runToCursor(const QString& location);
	void reverseContinueExecution();
	void reverseStepInto();
	void reverseStepOver();
	[[nodiscard]] bool supportsReverseExecution() const;

	// Breakpoints
	void insertBreakpoint(const QString& location);
	void removeBreakpoint(int breakpointId);
	void clearAllBreakpoints();
	void setBreakpointEnabled(int breakpointId, bool enabled);
	void toggleBreakpoint(const QString& location);
	void updateBreakpointCondition(int breakpointId, const QString& expr);
	void updateBreakpointIgnoreCount(int breakpointId, int ignoreCount);
	void updateBreakpointTemporary(int breakpointId, bool temporary);

	// Stack navigation
	void selectStackFrame(int frameIndex);

	// State access
	[[nodiscard]] const QVector<StackFrame>& stackFrames() const;
	[[nodiscard]] const std::vector<std::unique_ptr<DebugVariable>>& variables() const;
	[[nodiscard]] const QVector<ExecutionSnapshot>& executionHistory() const;
	[[nodiscard]] const ExecutionSnapshot* snapshotAt(int index) const;
	[[nodiscard]] const QSet<QString>& changedPaths() const;
	[[nodiscard]] const QVector<BreakpointInfo>& breakpoints() const;

	// Expression evaluation / raw MI
	void evaluateExpression(const QString& expression);
	void addWatchExpression(const QString& expression);
	void removeWatchExpression(const QString& expression);
	[[nodiscard]] const QStringList& watchExpressions() const;
	void sendRawCommand(const QString& cmd,
	                    std::function<void(const QString&)> cb = nullptr);
	void setVariable(const QString& fullPath, const QString& newValue);
	void requestDisassembly(const QString& file, int line, int instructionCount = 80);
	void requestDisassemblyAtLastStop(int instructionCount = 80);
	void dereferencePointer(const QString& pointerExpr,
							std::function<void(const QString& value,
											   const QString& type)> cb);
	void evaluateExpressionValue(const QString& expr,
								 std::function<void(const QString& value,
													const QString& type)> cb);
	// Supplies variables from a non-GDB backend while keeping every variables
	// UI (tree, graphical display and assistant) on the same model.
	void replaceExternalVariables(const QMap<QString, QString>& values);
	void replaceExternalStackFrames(const QVector<StackFrame>& frames);

signals:
	void targetRunning();
	void targetStarted();
	void targetStartFailed(const QString& message);
	void targetStopped();
	void targetExited(int exitCode);
	void stoppedAt(const QString& file, int line, const QString& function);
	void stoppedAtAddress(const QString& address);

	void stackFramesUpdated();
	void variablesUpdated();
	void breakpointsUpdated();
	void breakpointLinesChanged(const QString& file, const QSet<int>& lines);

	void snapshotCaptured(const ExecutionSnapshot& snapshot);
	void variableChangesDetected(const QVector<VariableChange>& changes,
								 int fromStep,
								 int toStep);

	void debuggerOutput(const QString& text);
	void downloadStarted();
	void downloadProgress(int percentage, qint64 bytesSent, qint64 totalBytes,
	                      const QString& section);
	void downloadFinished(bool success);
	void disassemblyUpdated(const QString& text);
	void reverseExecutionAvailabilityChanged();

private:
	Q_DISABLE_COPY_MOVE(DebuggerSession)

	// =======================
	// Command queue (tokened)
	// =======================
	struct PendingCommand {
		int token = 0;
		QString command;                         // without token prefix
		std::function<void(const QString&)> cb;  // receives full reply blob
	};

	void enqueueCommand(const QString& command,
						std::function<void(const QString&)> cb = nullptr);

	void processCommandQueue();

	void onDebuggerOutputReady();
	void onDebuggerFinished(int exitCode, QProcess::ExitStatus status);

	void dispatchDebuggerMessage(const QString& line);
	void handleResultRecord(int token, const QString& resultLine);
	void onTargetStoppedInternal(const QString& stopMessage);
	void handleBreakpointDeleted(const QString& resultLine);
	void handleBreakpointEvent(const QString& resultLine);
	void ensureReverseRecording();
	void executeReverseCommand(const QString& command);
	bool canStartExecutionCommand(const QString& command);

	// state requests
	void requestStopState();

	// parsing helpers
	void parseStackFromReply(const QString& replyBlob);
	void parseVarsFromReply(const QString& replyBlob);
	void requestWatchValues();

	// snapshot
	void finalizeSnapshotIfReady();
	void captureExecutionSnapshot();
	bool restoreHistoricalVariables();
	void computeVariableChanges(const ExecutionSnapshot& previous,
								const ExecutionSnapshot& current);

	[[nodiscard]] bool isRemoteTarget() const;
	[[nodiscard]] QString remoteSpec() const;

private:
	Backend m_backend = Backend::LldbMi;

	QString m_lastStopFile;
	QString m_lastStopFunction;
	int m_lastStopLine = 0;
	QString m_lastStopAddr;
	QString m_currentThreadId;

	bool m_captureDisassembly = false;
	QString m_disassemblyBuffer;

	QString m_gdbExecutable = "gdb";
	QString m_lldbMiExecutable = "/usr/local/bin/lldb-mi";

	TargetType m_targetType = TargetType::Local;
	ReverseMode m_reverseMode = ReverseMode::Auto;
	QStringList m_remoteConnectCommands;
	bool m_useExtendedRemote = false;
	QString m_remoteHost = "127.0.0.1";
	int m_remotePort = 3333;

	QString m_stlinkServerPath = "ST-LINK_gdbserver";
	int m_stlinkGdbPort = 4242;

	QProcess m_debuggerProcess;
	QProcess m_stlinkProcess;
	bool m_targetExecuting = false;

	bool m_commandInFlight = false;
	int  m_nextToken = 1;

	QQueue<PendingCommand> m_commandQueue;
	PendingCommand m_inFlight;
	QString m_inFlightReply;

	QVector<StackFrame> m_stackFrames;
	std::vector<std::unique_ptr<DebugVariable>> m_variables;
	QStringList m_watchExpressions;

	QVector<ExecutionSnapshot> m_executionHistory;
	enum class ReplayDirection { None, Backward, Forward };
	ReplayDirection m_replayDirection = ReplayDirection::None;
	int m_historyCursor = -1;
	bool m_restoredHistoricalVariables = false;
	QVector<BreakpointInfo> m_breakpoints;
	QSet<QString> m_changedPaths;
	bool m_reverseRecordingRequested = false;
	bool m_reverseRecordingFailed = false;
	bool m_reverseRecordingReady = false;

	int m_stepCounter = 0;
	bool m_pendingStack = false;
	bool m_pendingVariables = false;
	int m_pendingPointerExpansions = 0;
	int m_pendingAddressRequests = 0;
};
