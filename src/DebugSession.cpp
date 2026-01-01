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

#include <QDebug>
#include <QFileInfo>
#include <QTextStream>

static QString decodeCString(QString s) {
	if (s.startsWith("\"") && s.endsWith("\""))
		s = s.mid(1, s.length() - 2);

	s.replace("\\n", "\n");
	s.replace("\\t", "\t");
	s.replace("\\\"", "\"");
	s.replace("\\r", "\r");
	return s;
}

static QList<VarNode *> parseMiChildren(const QString &payload) {
	QList<VarNode *> list;
	QStringList entries = payload.split("},{");
	for (QString e : entries) {
		VarNode *n = new VarNode;
		if (e.contains("name=\""))
			n->name = e.section("name=\"", 1, 1).section("\"", 0, 0);
		if (e.contains("value=\""))
			n->value = e.section("value=\"", 1, 1).section("\"", 0, 0);
		if (e.contains("type=\""))
			n->type = e.section("type=\"", 1, 1).section("\"", 0, 0);
		if (e.contains("numchild=\"")) {
			int c = e.section("numchild=\"", 1, 1).section("\"", 0, 0).toInt();
			n->hasChildren = (c > 0);
		}
		if (e.contains("id=\""))
			n->varId = e.section("id=\"", 1, 1).section("\"", 0, 0);
		list.append(n);
	}
	return list;
}

static bool attachToTree(QList<VarNode *> &roots, const QString &parentId,
                         QList<VarNode *> children) {
	std::function<bool(VarNode *)> rec = [&](VarNode *node) -> bool {
		if (node->varId == parentId) {
			node->children = children;
			return true;
		}
		for (VarNode *c : node->children) {
			if (rec(c))
				return true;
		}
		return false;
	};

	for (VarNode *r : roots)
		if (rec(r))
			return true;

	return false;
}

DebugSession::DebugSession(QObject *parent) : QObject(parent) {
	connect(&m_proc, &QProcess::readyReadStandardOutput, this,
	        &DebugSession::onReadyReadStdout);

	connect(&m_proc,
	        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
	        &DebugSession::onProcessFinished);
}

void DebugSession::setBackend(Backend b) { m_backend = b; }

DebugSession::Backend DebugSession::backend() const { return m_backend; }

void DebugSession::start(const QString &programPath) {
	m_programPath = programPath;

	QFileInfo fi(programPath);
	if (!fi.exists()) {
		qWarning() << "Program does not exist:" << programPath;
		return;
	}

	QString debuggerExe;
	QStringList args;

	switch (m_backend) {
	case Backend::LLDB_MI:
		debuggerExe = "lldb-mi";
		args << "--interpreter=mi3";
		break;
	case Backend::GDB_MI:
		debuggerExe = "gdb";
		args << "--interpreter=mi2";
		break;
	}

	m_proc.start(debuggerExe, args);

	if (!m_proc.waitForStarted()) {
		qWarning() << "Debugger failed to start.";
		return;
	}

	enqueueCommand({QString("-file-exec-and-symbols \"%1\"").arg(programPath),
	                nullptr, true});
}

void DebugSession::execRun() { enqueueCommand({"-exec-run", nullptr, true}); }

void DebugSession::execContinue() {
	qDebug() << "[DBG] execContinue() called, m_busy=" << m_busy
	         << ", queue size=" << m_queue.size();
	enqueueCommand({"-exec-continue", nullptr, true});
}

void DebugSession::execStep() {
	qDebug() << "[DBG] execStep() called, m_busy=" << m_busy
	         << ", queue size=" << m_queue.size();
	enqueueCommand({"-exec-step", nullptr, true});
}

void DebugSession::execNext() {
	qDebug() << "[DBG] execNext() called, m_busy=" << m_busy
	         << ", queue size=" << m_queue.size();
	enqueueCommand({"-exec-next", nullptr, true});
}

void DebugSession::execFinish() {
	enqueueCommand({"-exec-finish", nullptr, true});
}

void DebugSession::execInterrupt() {
	enqueueCommand({"-exec-interrupt", nullptr, true});
}

void DebugSession::execUntil(const QString &loc) {
	if (loc.isEmpty())
		return;
	enqueueCommand({QString("-exec-until %1").arg(loc), nullptr, true});
}

void DebugSession::selectFrame(int index) {
	enqueueCommand(
	    {QString("-stack-select-frame %1").arg(index), nullptr, true});
	requestRefresh();
}

void DebugSession::insertBreakpoint(const QString &loc) {
	if (loc.isEmpty())
		return;
	enqueueCommand({QString("-break-insert %1").arg(loc), nullptr, true});
	enqueueCommand({"-break-list", nullptr, false});
}

void DebugSession::deleteBreakpoint(int number) {
	if (number <= 0)
		return;
	enqueueCommand({QString("-break-delete %1").arg(number), nullptr, true});
	enqueueCommand({"-break-list", nullptr, false});
}

void DebugSession::clearAllBreakpoints() {
	enqueueCommand({"-break-delete", nullptr, true});
	enqueueCommand({"-break-list", nullptr, false});
}

void DebugSession::toggleBreakpointEnabled(int n, bool en) {
	enqueueCommand(
	    {QString("-break-%1 %2").arg(en ? "enable" : "disable").arg(n), nullptr,
	     true});
	enqueueCommand({"-break-list", nullptr, false});
}

void DebugSession::toggleBreakpointAt(const QString &file, int line) {
	for (const auto &bp : m_bps) {
		if (bp.file == file && bp.line == line) {
			deleteBreakpoint(bp.number);
			return;
		}
	}

	insertBreakpoint(QString("%1:%2").arg(file).arg(line));
}

void DebugSession::sendCommand(const QString &cmd) {
	const QString translated = translateUserCommand(cmd);
	enqueueCommand({translated, nullptr, true});
}

QString DebugSession::translateUserCommand(const QString &cmd) const {
	QString trimmed = cmd.trimmed();
	if (trimmed.isEmpty())
		return trimmed;

	if (trimmed.startsWith('-'))
		return trimmed;

	// Small logic command map -> MI (valid for LLDB-MI and GDB-MI)
	if (trimmed == "run" || trimmed == "r")
		return "-exec-run";
	if (trimmed == "cont" || trimmed == "continue" || trimmed == "c")
		return "-exec-continue";
	if (trimmed == "next" || trimmed == "n")
		return "-exec-next";
	if (trimmed == "step" || trimmed == "s")
		return "-exec-step";
	if (trimmed == "finish")
		return "-exec-finish";
	if (trimmed == "quit" || trimmed == "q")
		return "-gdb-exit";

	return trimmed;
}

QList<StackFrame> DebugSession::stackFrames() const { return m_stack; }

QList<VariableInfo> DebugSession::variables() const { return m_vars; }

QList<VarNode *> DebugSession::complexVariables() const { return m_cvars; }

QList<BreakpointInfo> DebugSession::breakpoints() const { return m_bps; }

QString DebugSession::currentFile() const { return m_currentFile; }

int DebugSession::currentLine() const { return m_currentLine; }

void DebugSession::sendMiDirect(const QString &cmd) {
	qDebug().noquote() << "[MI SEND]" << cmd;
	m_proc.write((cmd + "\n").toUtf8());
}

void DebugSession::enqueueCommand(const MiCommand &c) {
	m_queue.enqueue(c);
	processQueue();
}

void DebugSession::processQueue() {
	if (m_busy || m_queue.isEmpty())
		return;

	m_busy = true;
	m_currentCmd = m_queue.dequeue();
	sendMiDirect(m_currentCmd.text);
}

void DebugSession::onReadyReadStdout() {
	QByteArray raw = m_proc.readAllStandardOutput();
	qDebug().noquote() << "[LLDB-MI]" << raw;

	m_buffer += raw;

	QList<QByteArray> rawLines = m_buffer.split('\n');
	m_buffer.clear();

	for (const QByteArray &ba : rawLines) {
		QString line = QString::fromUtf8(ba).trimmed();
		if (!line.isEmpty())
			parseMiLine(line);
	}
}

void DebugSession::onProcessFinished(int exitCode,
                                     QProcess::ExitStatus status) {
	emit debuggerExited(exitCode, status);
}

// ============================================================================
// Parsing MI
// ============================================================================
void DebugSession::parseMiLine(const QString &line) {
	// All'inizio di parseMiLine()
	QString l = line.trimmed();

	// prompt MI (gdb) — ignora completamente
	if (l == "(gdb)" || l == "(lldb)" || l == ">")
		return;

	// MI console-stream-output
	if (line.startsWith("~\"")) {
		QString text = line.mid(2);
		if (text.endsWith("\""))
			text.chop(1);
		emit outputReceived(decodeCString(text));
		return;
	}

	// Target program stdout
	if (line.startsWith("@\"")) {
		QString text = line.mid(2);
		if (text.endsWith("\""))
			text.chop(1);
		emit outputReceived(decodeCString(text));
		return;
	}

	// Log stream
	if (line.startsWith("&\"")) {
		QString text = line.mid(2);
		if (text.endsWith("\""))
			text.chop(1);
		emit outputReceived(decodeCString(text));
		return;
	}

	// NEW — generic raw debugger output (prompt, warnings, anything not MI)
	// This FIXES the missing "(gdb)" output in UI
	if (!line.startsWith("^") && !line.startsWith("*") &&
	    !line.startsWith("=")) {
		emit outputReceived(line);
		return;
	}

	// End of MI command
	if (line.startsWith("^done") || line.startsWith("^error")) {
		m_busy = false;
		if (m_currentCmd.callback)
			m_currentCmd.callback(line);
		processQueue();
	}
	// --- eventi async su breakpoint (LLDB-MI / GDB-MI) ---
	if (line.startsWith("=breakpoint-created") ||
	    line.startsWith("=breakpoint-modified")) {
		handleBreakpointEvent(line);
		return;
	}

	if (line.startsWith("=breakpoint-deleted")) {
		handleBreakpointDeleted(line);
		return;
	}

	// Async / specific MI responses
	if (line.startsWith("*stopped"))
		handleStopped(line);
	else if (line.startsWith("^done,stack=") ||
	         line.startsWith("^done,stack-frames="))
		handleStackList(line);
	else if (line.startsWith("^done,variables="))
		handleVarList(line);
	else if (line.startsWith("^done,children="))
		parseComplexVarTree(line);
	else if (line.startsWith("^done,frame="))
		handleFrameInfo(line);
	else if (line.startsWith("^done,BreakpointTable="))
		handleBreakpointList(line);

	if (!m_pendingStack && !m_pendingVars)
		emit sessionUpdated();
}

// ============================================================================
// Gestione stopped / fetchData
// ============================================================================
void DebugSession::handleStopped(const QString &line) {
	m_pendingExec = false;

	if (m_busy) {
		m_busy = false;
		if (m_currentCmd.callback)
			m_currentCmd.callback(line);
		processQueue();
	}

	if (line.contains("fullname=\""))
		m_currentFile = line.section("fullname=\"", 1, 1).section("\"", 0, 0);
	else if (line.contains("file=\""))
		m_currentFile = line.section("file=\"", 1, 1).section("\"", 0, 0);

	if (line.contains("line=\""))
		m_currentLine =
		    line.section("line=\"", 1, 1).section("\"", 0, 0).toInt();

	emit sessionUpdated();

	fetchData();
}

void DebugSession::fetchData() {
	m_pendingStack = true;
	m_pendingVars = true;

	enqueueCommand({"-stack-list-frames", nullptr, false});
	enqueueCommand({"-stack-info-frame", nullptr, false});
	enqueueCommand({"-stack-list-variables --all-values", nullptr, false});
	enqueueCommand({"-break-list", nullptr, false});

	if (m_useComplexVarView)
		fetchComplexVars();
}

void DebugSession::requestRefresh() { fetchData(); }

// ============================================================================
// Stack / vars / frame / breakpoints
// ============================================================================
void DebugSession::handleStackList(const QString &line) {
	QList<StackFrame> frames;
	QString cpy = line;
	cpy.remove("^done,stack=");
	cpy.remove("^done,stack-frames=");

	QStringList entries = cpy.split("},{");
	for (QString e : entries) {
		StackFrame f;
		if (e.contains("level=\""))
			f.level = e.section("level=\"", 1, 1).section("\"", 0, 0);
		if (e.contains("file=\""))
			f.file = e.section("file=\"", 1, 1).section("\"", 0, 0);
		if (e.contains("line=\""))
			f.line = e.section("line=\"", 1, 1).section("\"", 0, 0).toInt();
		if (e.contains("func=\""))
			f.function = e.section("func=\"", 1, 1).section("\"", 0, 0);
		frames.append(f);
	}

	m_stack = frames;
	m_pendingStack = false;

	for (const StackFrame &f : frames) {
		if (f.level == "0") {
			m_currentFile = f.file;
			m_currentLine = f.line;
			break;
		}
	}
}

void DebugSession::handleVarList(const QString &line) {
	QList<VariableInfo> vars;
	QString cpy = line;
	cpy.remove("^done,variables=");

	QStringList entries = cpy.split("},{");
	for (QString e : entries) {
		VariableInfo v;
		if (e.contains("name=\""))
			v.name = e.section("name=\"", 1, 1).section("\"", 0, 0);
		if (e.contains("value=\""))
			v.value = e.section("value=\"", 1, 1).section("\"", 0, 0);
		if (e.contains("type=\""))
			v.type = e.section("type=\"", 1, 1).section("\"", 0, 0);

		vars.append(v);
	}

	m_vars = vars;
	m_pendingVars = false;

	// --- LLDB FIX (optional):
    /*
	for (auto &v : m_vars) {
		if (v.type.isEmpty() && !v.name.isEmpty()) {
			QString cmd =
			    QString("-data-evaluate-expression \"expr -T %1\"").arg(v.name);
			enqueueCommand({cmd, nullptr, false});
		}
	}
	*/
}

void DebugSession::handleFrameInfo(const QString &line) {
	QString cpy = line;
	cpy.remove("^done,frame=");

	if (cpy.contains("fullname=\""))
		m_currentFile = cpy.section("fullname=\"", 1, 1).section("\"", 0, 0);
	else if (cpy.contains("file=\""))
		m_currentFile = cpy.section("file=\"", 1, 1).section("\"", 0, 0);

	if (cpy.contains("line=\""))
		m_currentLine =
		    cpy.section("line=\"", 1, 1).section("\"", 0, 0).toInt();
}

void DebugSession::handleBreakpointList(const QString &line) {
	QList<BreakpointInfo> out;
	QString s = line;
	s.remove("^done,BreakpointTable=");

	QStringList bps = s.split("},{");
	for (QString e : bps) {
		BreakpointInfo b;
		if (e.contains("number=\""))
			b.number = e.section("number=\"", 1, 1).section("\"", 0, 0).toInt();
		if (e.contains("file=\""))
			b.file = e.section("file=\"", 1, 1).section("\"", 0, 0);
		if (e.contains("fullname=\""))
			b.file = e.section("fullname=\"", 1, 1).section("\"", 0, 0);
		if (e.contains("line=\""))
			b.line = e.section("line=\"", 1, 1).section("\"", 0, 0).toInt();
		b.enabled = !e.contains("enabled=\"n\"");
		out.append(b);
	}

	m_bps = out;
	emit breakpointsChanged(m_bps);
}

void DebugSession::handleBreakpointEvent(const QString &line) {
	// =breakpoint-created,bkpt={number="2",type="breakpoint",disp="keep",enabled="y",...,file="prova.c",fullname="...",line="9",...}

	int pos = line.indexOf("bkpt=");
	if (pos < 0)
		return;

	QString e = line.mid(pos);
	if (e.startsWith("bkpt="))
		e.remove(0, 5);
	e = e.trimmed();
	if (e.startsWith("{"))
		e = e.mid(1);
	if (e.endsWith("}"))
		e.chop(1);

	BreakpointInfo b;

	if (e.contains("number=\""))
		b.number = e.section("number=\"", 1, 1).section("\"", 0, 0).toInt();
	if (e.contains("file=\""))
		b.file = e.section("file=\"", 1, 1).section("\"", 0, 0);
	if (e.contains("fullname=\""))
		b.file = e.section("fullname=\"", 1, 1).section("\"", 0, 0);
	if (e.contains("line=\""))
		b.line = e.section("line=\"", 1, 1).section("\"", 0, 0).toInt();
	b.enabled = !e.contains("enabled=\"n\"");

	bool updated = false;
	for (int i = 0; i < m_bps.size(); ++i) {
		if (m_bps[i].number == b.number) {
			m_bps[i] = b;
			updated = true;
			break;
		}
	}
	if (!updated)
		m_bps.append(b);

	emit breakpointsChanged(m_bps);
}

void DebugSession::handleBreakpointDeleted(const QString &line) {
	// =breakpoint-deleted,id="2"
	int num = -1;

	if (line.contains("id=\""))
		num = line.section("id=\"", 1, 1).section("\"", 0, 0).toInt();
	else if (line.contains("number=\""))
		num = line.section("number=\"", 1, 1).section("\"", 0, 0).toInt();

	if (num < 0)
		return;

	for (int i = 0; i < m_bps.size();) {
		if (m_bps[i].number == num)
			m_bps.removeAt(i);
		else
			++i;
	}

	emit breakpointsChanged(m_bps);
}

void DebugSession::fetchComplexVars() {
	m_cvars.clear();
	enqueueCommand({"-var-create root * \"*\"", nullptr, false});
	enqueueCommand({"-var-list-children --all-values root", nullptr, false});
}

void DebugSession::parseComplexVarTree(const QString &line) {
	QString payload = line;
	payload.remove("^done,children=");

	QList<VarNode *> nodes = parseMiChildren(payload);

	if (m_cvars.isEmpty()) {
		m_cvars = nodes;
	} else {
		QString id;
		if (line.contains("id=\""))
			id = line.section("id=\"", 1, 1).section("\"", 0, 0);

		if (!id.isEmpty()) {
			attachToTree(m_cvars, id, nodes);
		}
	}

	for (VarNode *n : nodes) {
		if (n->hasChildren && !n->varId.isEmpty())
			enqueueCommand(
			    {QString("-var-list-children --all-values %1").arg(n->varId),
			     nullptr, false});
	}
}

QString DebugSession::evaluateExpression(const QString &expr) {
	if (expr.isEmpty())
		return {};

	QString cmd = QString("-data-evaluate-expression \"%1\"").arg(expr);
	enqueueCommand({cmd, nullptr, true});

	return {};
}
