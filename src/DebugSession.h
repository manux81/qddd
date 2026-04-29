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

	explicit DebuggerSession(QObject* parent = nullptr);
	~DebuggerSession() override;

	void setBackend(Backend backend);
	void setGdbExecutable(const QString& path);
	void setLldbMiExecutable(const QString& path);
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
	void sendRawCommand(const QString& cmd);
	void setVariable(const QString& fullPath, const QString& newValue);
	void dereferencePointer(const QString& pointerExpr,
							std::function<void(const QString& value,
											   const QString& type)> cb);
	void evaluateExpressionValue(const QString& expr,
								 std::function<void(const QString& value,
													const QString& type)> cb);

signals:
	void targetRunning();
	void targetStarted();
	void targetStopped();
	void targetExited(int exitCode);
	void stoppedAt(const QString& file, int line, const QString& function);

	void stackFramesUpdated();
	void variablesUpdated();
	void breakpointsUpdated();
	void breakpointLinesChanged(const QString& file, const QSet<int>& lines);

	void snapshotCaptured(const ExecutionSnapshot& snapshot);
	void variableChangesDetected(const QVector<VariableChange>& changes,
								 int fromStep,
								 int toStep);

	void debuggerOutput(const QString& text);

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

	// state requests
	void requestStopState();

	// parsing helpers
	void parseStackFromReply(const QString& replyBlob);
	void parseVarsFromReply(const QString& replyBlob);

	// snapshot
	void finalizeSnapshotIfReady();
	void captureExecutionSnapshot();
	void computeVariableChanges(const ExecutionSnapshot& previous,
								const ExecutionSnapshot& current);

private:
	Backend m_backend = Backend::LldbMi;

	QString m_gdbExecutable = "gdb";
	QString m_lldbMiExecutable = "/usr/local/bin/lldb-mi";

	QProcess m_debuggerProcess;

	bool m_commandInFlight = false;
	int  m_nextToken = 1;

	QQueue<PendingCommand> m_commandQueue;
	PendingCommand m_inFlight;
	QString m_inFlightReply;

	QVector<StackFrame> m_stackFrames;
	std::vector<std::unique_ptr<DebugVariable>> m_variables;

	QVector<ExecutionSnapshot> m_executionHistory;
	QVector<BreakpointInfo> m_breakpoints;
	QSet<QString> m_changedPaths;

	int m_stepCounter = 0;
	bool m_pendingStack = false;
	bool m_pendingVariables = false;
	int m_pendingPointerExpansions = 0;
	int m_pendingAddressRequests = 0;
};
