#include "DebuggerTransport.h"
#pragma once
#include "DebugSession.h"
#include <QProcess>
#include <QQueue>
#include <QRegularExpression>
#include <QTimer>
#include "MiStreamBuffer.h"

// GDB/MI implementation; views depend only on DebuggerSession.
class GdbMiSession final : public DebuggerSession
{
	Q_OBJECT

public:
	explicit GdbMiSession(QObject* parent = nullptr, std::unique_ptr<DebuggerTransport> transport = {});
	~GdbMiSession() override;

	void setBackend(Backend backend) override;
	void setGdbExecutable(const QString& path) override;
	void setLldbMiExecutable(const QString& path) override;
	void setTargetType(TargetType type) override;
	void setReverseMode(ReverseMode mode) override;
	void setRemoteEndpoint(const QString& host, int port) override;
	void setRemoteConnectCommands(const QStringList& commands, bool extendedRemote = false) override;
	void setStlinkServerPath(const QString& path) override;
	void setStlinkGdbPort(int port) override;
	void setCommandTimeoutMs(int timeoutMs) override;
	void startSession(const QString& executablePath) override;
	void terminateSession() override;

	[[nodiscard]] bool isRunning() const override;

	// Execution control
	void run() override;
	void continueExecution() override;
	void stepInto() override;
	void stepOver() override;
	void stepOut() override;
	void interruptExecution() override;
	void runToCursor(const QString& location) override;
	void reverseContinueExecution() override;
	void reverseStepInto() override;
	void reverseStepOver() override;
	[[nodiscard]] bool supportsReverseExecution() const override;

	// Breakpoints
	void insertBreakpoint(const BreakpointRequest& request) override;
	void insertBreakpoint(const QString& location) override;
	void removeBreakpoint(int breakpointId) override;
	void clearAllBreakpoints() override;
	void setBreakpointEnabled(int breakpointId, bool enabled) override;
	void toggleBreakpoint(const QString& location) override;
	void updateBreakpointCondition(int breakpointId, const QString& expr) override;
	void updateBreakpointIgnoreCount(int breakpointId, int ignoreCount) override;
	void updateBreakpointTemporary(int breakpointId, bool temporary) override;

	// Stack navigation
	void selectStackFrame(int frameIndex) override;
	void selectThread(const QString& id) override;
	const QVector<DebugThread>& threads() const override { return m_threads; }
	QString selectedThread() const override { return m_currentThreadId; }

	// State access
	[[nodiscard]] const QVector<StackFrame>& stackFrames() const override;
	[[nodiscard]] const std::vector<std::unique_ptr<DebugVariable>>& variables() const override;
	[[nodiscard]] const QVector<ExecutionSnapshot>& executionHistory() const override;
	[[nodiscard]] const ExecutionSnapshot* snapshotAt(int index) const override;
	[[nodiscard]] const RuntimeObjectGraph& objectGraph() const override { return m_objectGraph; }
	[[nodiscard]] const RuntimeGraphDiff& graphChanges() const override { return m_graphChanges; }
	[[nodiscard]] const QSet<QString>& changedPaths() const override;
	[[nodiscard]] const QVector<BreakpointInfo>& breakpoints() const override;

	// Expression evaluation / raw MI
	void inspectValue(const QString& expression, std::function<void(SemanticValue)> callback) override;
	void readMemory(const QString& address, int byteCount, std::function<void(MemoryRead)> callback) override;
	void evaluateExpression(const QString& expression) override;
	void addWatchExpression(const QString& expression) override;
	void removeWatchExpression(const QString& expression) override;
	void replaceWatchExpression(const QString& oldExpression,
	                            const QString& newExpression) override;
	void setWatchExpressionEnabled(const QString& expression, bool enabled) override;
	[[nodiscard]] bool isWatchExpressionEnabled(const QString& expression) const override;
	[[nodiscard]] const QStringList& watchExpressions() const override;
	void setValueFormat(const QString& expression, DebugValueFormat format) override;
	[[nodiscard]] DebugValueFormat valueFormat(const QString& expression) const override;
	[[nodiscard]] QString formattedValue(const DebugVariable* variable) const override;
	void sendRawCommand(const QString& cmd,
	                    std::function<void(const QString&)> cb = nullptr) override;
	void setVariable(const QString& fullPath, const QString& newValue) override;
	void requestDisassembly(const QString& file, int line, int instructionCount = 80) override;
	void requestDisassemblyAtLastStop(int instructionCount = 80) override;
	void dereferencePointer(const QString& pointerExpr,
							std::function<void(const QString& value,
											   const QString& type)> cb) override;
	void evaluateExpressionValue(const QString& expr,
								 std::function<void(const QString& value,
													const QString& type)> cb) override;
	// Supplies variables from a non-GDB backend while keeping every variables
	// UI (tree, graphical display and assistant) on the same model.
	void replaceExternalVariables(const QMap<QString, QString>& values) override;
	void replaceExternalStackFrames(const QVector<StackFrame>& frames) override;

private:
	Q_DISABLE_COPY_MOVE(GdbMiSession)

	// =======================
	// Command queue (tokened)
	// =======================
	struct PendingCommand {
		int token = 0;
		quint64 generation = 0;
		QString command;                         // without token prefix
		std::function<void(const QString&)> cb;  // receives full reply blob
	};

	void enqueueCommand(const QString& command,
						std::function<void(const QString&)> cb = nullptr);

	void processCommandQueue();

	void onDebuggerOutputReady();
	void onDebuggerFinished(int exitCode, QProcess::ExitStatus status);
	void onCommandTimeout();
	void consumeDebuggerOutput(const QByteArray& data);
	void resetSessionState();
	void abortCommandChannel(const QString& reason);

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
	void requestWatchValue(const QString& expression);
	void upsertWatchVariable(const QString& expression,
	                        const QString& value,
	                        const QString& type,
	                        bool enabled);

	// snapshot
	void finalizeSnapshotIfReady(quint64 refreshGeneration);
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
	int m_selectedFrame = 0;

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

	std::unique_ptr<DebuggerTransport> m_transport;
	QProcess m_stlinkProcess;
	MiStreamBuffer m_debuggerOutputBuffer;
	QTimer m_commandTimeoutTimer;
	bool m_targetExecuting = false;
	bool m_commandChannelReliable = true;
	quint64 m_sessionGeneration = 0;
	int m_commandTimeoutMs = 15000;

	bool m_commandInFlight = false;
	int  m_nextToken = 1;

	QQueue<PendingCommand> m_commandQueue;
	PendingCommand m_inFlight;
	QString m_inFlightReply;

	QVector<DebugThread> m_threads;
	QVector<StackFrame> m_stackFrames;
	std::vector<std::unique_ptr<DebugVariable>> m_variables;
	QStringList m_watchExpressions;
	QSet<QString> m_disabledWatchExpressions;
	QHash<QString, QString> m_watchValueCache;
	QHash<QString, QString> m_watchTypeCache;
	QHash<QString, DebugValueFormat> m_valueFormats;

	RuntimeObjectGraph m_objectGraph;
	RuntimeGraphDiff m_graphChanges;
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
	quint64 m_stopStateGeneration = 0;
	bool m_snapshotArmed = false;
	bool m_pendingStack = false;
	bool m_pendingVariables = false;
	int m_pendingPointerExpansions = 0;
	int m_pendingAddressRequests = 0;
	int m_pendingWatchRequests = 0;
};
