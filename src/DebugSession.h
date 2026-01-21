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
#include <QList>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <functional>

struct StackFrame {
	QString level;
	QString file;
	int line = 0;
	QString function;
};

struct VarNode {
	QString name;
	QString value;
	QString type;
	QString addr;
	QString varId;
	bool hasChildren = false;
	bool isPointer = false;
	QList<VarNode *> children;
	VarNode* parent = nullptr;
};

struct BreakpointInfo {
	int number = 0;
	QString file;
	int line = 0;
	bool enabled = true;
};

class DebugSession : public QObject {
	Q_OBJECT

  public:
	enum class Backend { LLDB_MI, GDB_MI };

	explicit DebugSession(QObject *parent = nullptr);

	void setBackend(Backend b);
	Backend backend() const;

	void start(const QString &programPath);

	void execRun();
	void execContinue();
	void execStep();
	void execNext();
	void execFinish();
	void execInterrupt();
	void execUntil(const QString &loc);
	void selectFrame(int index);

	void insertBreakpoint(const QString &loc);
	void deleteBreakpoint(int number);
	void clearAllBreakpoints();
	void toggleBreakpointEnabled(int n, bool en);
	void toggleBreakpointAt(const QString &file, int line);


	void sendCommand(const QString &cmd);
	QString evaluateExpression(const QString &expr);

	QList<StackFrame> stackFrames() const;
	QList<VarNode *> complexVariables() const;
	QList<BreakpointInfo> breakpoints() const;

	QString currentFile() const;
	int currentLine() const;

	void setUseComplexVarView(bool b) { m_useComplexVarView = b; }
	quint64 stepCounter() const { return m_stepCounter; }

  signals:
	void outputReceived(const QString &text);
	void sessionUpdated();
	void breakpointsChanged(const QList<BreakpointInfo> &list);
	void debuggerExited(int exitCode, QProcess::ExitStatus status);
	void complexVariablesUpdated(QList<VarNode *> roots);

  private slots:
	void onReadyReadStdout();
	void onProcessFinished(int exitCode, QProcess::ExitStatus status);

  private:
	struct MiCommand {
		QString text;
		std::function<void(const QString &)> callback;
		bool isUser = false;
	};

	void sendMiDirect(const QString &cmd);
	void enqueueCommand(const MiCommand &c);
	void processQueue();

	void parseMiLine(const QString &line);
	void handleStopped(const QString &line);
	void fetchData();
	void requestRefresh();
	void handleStackList(const QString &line);
	void handleVarList(const QString &line);
	void handleFrameInfo(const QString &line);
	void handleBreakpointList(const QString &line);
	void handleBreakpointEvent(const QString &line);
	void handleBreakpointDeleted(const QString &line);

	void fetchComplexVars(VarNode* node);
	void parseComplexVarTree(const QString &line);
	QString translateUserCommand(const QString &cmd) const;

  private:
	QProcess m_proc;
	Backend m_backend = Backend::LLDB_MI;
	QString m_programPath;
	QByteArray m_buffer;

	QQueue<MiCommand> m_queue;
	bool m_busy = false;
	MiCommand m_currentCmd;

	QList<StackFrame> m_stack;
	QList<VarNode *> m_cvars;
	QList<BreakpointInfo> m_bps;

	QString m_currentFile;
	int m_currentLine = 0;

	bool m_pendingStack = false;
	bool m_pendingVars = false;
	bool m_pendingExec = false;
	bool m_useComplexVarView = false;
	quint64 m_stepCounter = 0U;
};
