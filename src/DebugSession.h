#include "TypeVisualizer.h"
#include "RuntimeObjectGraph.h"
#include "MemoryRead.h"
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
#include <QVector>
#include <QHash>
#include <QSet>
#include <QString>
#include <memory>
#include <vector>
#include <functional>

// ============================================================================
// Forward declarations
// ============================================================================

struct DebugVariable;
struct ExecutionSnapshot;
struct VariableChange;

enum class DebugValueFormat {
	Natural,
	Hexadecimal,
	Decimal,
	Octal,
	Binary,
	Character
};

QString debugValueFormatLabel(DebugValueFormat format);
QString formatDebugValue(const QString& value, DebugValueFormat format);

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



struct DebugThread { QString id; QString name; QString state; };

struct BreakpointRequest {
    enum Kind { Source, Function, Hardware, WriteWatch, ReadWatch, AccessWatch, Catch };
    Kind kind = Source;
    QString location;
    QString condition;
    int ignoreCount = 0;
    bool temporary = false;
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
	// Debugger-ready expression for this value.  Unlike the display name it
	// preserves dereference/member syntax for children of pointer displays.
	QString expression;
	QString value;
	QString type;
	QString address;
	// Structured destination identity for pointer values. Views must use this
	// instead of interpreting the presentation string in value.
	QString pointeeAddress;

	bool isPointer   = false;
	bool hasChildren = false;
	bool isWatch     = false;
	bool enabled     = true;

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
	QString threadId;
	QHash<QString, QString> objectIds;
	int frame = 0;
	QSet<QString> changedPaths;

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

class DebuggerSession : public QObject
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

	explicit DebuggerSession(QObject* parent = nullptr) : QObject(parent) {}
	~DebuggerSession() override = default;

	virtual void setBackend(Backend backend) = 0;
	virtual void setGdbExecutable(const QString& path) = 0;
	virtual void setLldbMiExecutable(const QString& path) = 0;
	virtual void setTargetType(TargetType type) = 0;
	virtual void setReverseMode(ReverseMode mode) = 0;
	virtual void setRemoteEndpoint(const QString& host, int port) = 0;
	virtual void setRemoteConnectCommands(const QStringList& commands, bool extendedRemote = false) = 0;
	virtual void setStlinkServerPath(const QString& path) = 0;
	virtual void setStlinkGdbPort(int port) = 0;
	virtual void setCommandTimeoutMs(int timeoutMs) = 0;
	virtual void startSession(const QString& executablePath) = 0;
	virtual void terminateSession() = 0;

	virtual bool isRunning() const = 0;

	// Execution control
	virtual void run() = 0;
	virtual void continueExecution() = 0;
	virtual void stepInto() = 0;
	virtual void stepOver() = 0;
	virtual void stepOut() = 0;
	virtual void interruptExecution() = 0;
	virtual void runToCursor(const QString& location) = 0;
	virtual void reverseContinueExecution() = 0;
	virtual void reverseStepInto() = 0;
	virtual void reverseStepOver() = 0;
	virtual bool supportsReverseExecution() const = 0;

	// Breakpoints
	virtual void insertBreakpoint(const BreakpointRequest& request) = 0;
	virtual void insertBreakpoint(const QString& location) = 0;
	virtual void removeBreakpoint(int breakpointId) = 0;
	virtual void clearAllBreakpoints() = 0;
	virtual void setBreakpointEnabled(int breakpointId, bool enabled) = 0;
	virtual void toggleBreakpoint(const QString& location) = 0;
	virtual void updateBreakpointCondition(int breakpointId, const QString& expr) = 0;
	virtual void updateBreakpointIgnoreCount(int breakpointId, int ignoreCount) = 0;
	virtual void updateBreakpointTemporary(int breakpointId, bool temporary) = 0;

	// Stack navigation
	virtual void selectStackFrame(int frameIndex) = 0;
	virtual void selectThread(const QString& id) = 0;
	virtual const QVector<DebugThread>& threads() const  = 0;
	virtual QString selectedThread() const  = 0;

	// State access
	virtual const QVector<StackFrame>& stackFrames() const = 0;
	virtual const std::vector<std::unique_ptr<DebugVariable>>& variables() const = 0;
	virtual const QVector<ExecutionSnapshot>& executionHistory() const = 0;
	virtual const ExecutionSnapshot* snapshotAt(int index) const = 0;
	virtual const RuntimeObjectGraph& objectGraph() const  = 0;
	virtual const RuntimeGraphDiff& graphChanges() const  = 0;
	virtual const QSet<QString>& changedPaths() const = 0;
	virtual const QVector<BreakpointInfo>& breakpoints() const = 0;

	// Expression evaluation / raw MI
	virtual void inspectValue(const QString& expression, std::function<void(SemanticValue)> callback) = 0;
	virtual void readMemory(const QString& address, int byteCount, std::function<void(MemoryRead)> callback) = 0;
	virtual void evaluateExpression(const QString& expression) = 0;
	virtual void addWatchExpression(const QString& expression) = 0;
	virtual void removeWatchExpression(const QString& expression) = 0;
	virtual void replaceWatchExpression(const QString& oldExpression,
	                            const QString& newExpression) = 0;
	virtual void setWatchExpressionEnabled(const QString& expression, bool enabled) = 0;
	virtual bool isWatchExpressionEnabled(const QString& expression) const = 0;
	virtual const QStringList& watchExpressions() const = 0;
	virtual void setValueFormat(const QString& expression, DebugValueFormat format) = 0;
	virtual DebugValueFormat valueFormat(const QString& expression) const = 0;
	virtual QString formattedValue(const DebugVariable* variable) const = 0;
	virtual void sendRawCommand(const QString& cmd,
	                    std::function<void(const QString&)> cb = nullptr) = 0;
	virtual void setVariable(const QString& fullPath, const QString& newValue) = 0;
	virtual void requestDisassembly(const QString& file, int line, int instructionCount = 80) = 0;
	virtual void requestDisassemblyAtLastStop(int instructionCount = 80) = 0;
	virtual void dereferencePointer(const QString& pointerExpr,
							std::function<void(const QString& value,
											   const QString& type)> cb) = 0;
	virtual void evaluateExpressionValue(const QString& expr,
								 std::function<void(const QString& value,
													const QString& type)> cb) = 0;
	// Supplies variables from a non-GDB backend while keeping every variables
	// UI (tree, graphical display and assistant) on the same model.
	virtual void replaceExternalVariables(const QMap<QString, QString>& values) = 0;
	virtual void replaceExternalStackFrames(const QVector<StackFrame>& frames) = 0;

signals:
	void targetRunning();
	void targetStarted();
	void targetStartFailed(const QString& message);
	void targetStopped();
	void targetExited(int exitCode);
	void stoppedAt(const QString& file, int line, const QString& function);
	void stoppedAtAddress(const QString& address);

	void threadsUpdated();
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

};

