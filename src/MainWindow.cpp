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

#include "MainWindow.h"

#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QToolBar>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QTabWidget>

MainWindow::MainWindow(const QString &initialProgram, QWidget *parent)
    : QMainWindow(parent), m_session(std::make_unique<DebuggerSession>(this)),
      m_currentProgram(initialProgram) {
	setupUi();
	setupMenusAndToolbars();

	m_consoleWidget->setSession(m_session.get());
	m_stackView->setSession(m_session.get());
	m_variablesView->setSession(m_session.get());
	m_sourceEditor->setSession(m_session.get());
	m_graphicalView->setSession(m_session.get());

	connect(m_session.get(), &DebuggerSession::targetStopped, this,
	        &MainWindow::onTargetStopped);

	connect(m_session.get(), &DebuggerSession::breakpointLinesChanged, this,
	        [this](const QString &file, const QSet<int> &lines) {
		        SourceEditor* editor = file.isEmpty()
			        ? currentSourceEditor()
			        : ensureSourceTabForFile(file);
		        if (editor)
			        editor->setBreakpointLines(lines);
	        });

	connect(m_session.get(), &DebuggerSession::snapshotCaptured, this,
	        [this](const ExecutionSnapshot &) { m_graphicalView->refresh(); });
	connect(m_session.get(), &DebuggerSession::variablesUpdated, m_graphicalView,
				  &GraphicalVariablesView::refresh);

	connect(m_session.get(), &DebuggerSession::stoppedAt, this,
	        [this](const QString& file, int line, const QString&) {
		        showSourceLocation(file, line);
	        });

	if (!m_currentProgram.isEmpty()) {
		startDebugger(m_currentProgram);
	}
}

void MainWindow::setupUi() {
	setDockOptions(QMainWindow::AllowTabbedDocks |
	               QMainWindow::AllowNestedDocks);

	m_sourceTabs = new QTabWidget(this);
	m_sourceTabs->setDocumentMode(true);
	m_sourceTabs->setMovable(true);
	m_sourceTabs->setTabsClosable(false);
	setCentralWidget(m_sourceTabs);

	auto* initialEditor = new SourceEditor(m_sourceTabs);
	m_sourceEditor = initialEditor;
	wireSourceEditor(initialEditor);
	m_sourceTabs->addTab(initialEditor, tr("Source"));

	connect(m_sourceTabs, &QTabWidget::currentChanged, this, [this](int idx) {
		auto* w = m_sourceTabs ? m_sourceTabs->widget(idx) : nullptr;
		auto* ed = qobject_cast<SourceEditor*>(w);
		if (ed)
			m_sourceEditor = ed;
	});

	m_varsDock = new QDockWidget(tr("Variables"), this);
	m_variablesView = new VariablesView(m_varsDock);
	m_varsDock->setWidget(m_variablesView);
	addDockWidget(Qt::RightDockWidgetArea, m_varsDock);

	m_stackDock = new QDockWidget(tr("Stack"), this);
	m_stackView = new StackView(m_stackDock);
	m_stackDock->setWidget(m_stackView);
	addDockWidget(Qt::RightDockWidgetArea, m_stackDock);
	tabifyDockWidget(m_varsDock, m_stackDock);

	m_dataDock = new QDockWidget(tr("Data Display"), this);
    m_graphicalView = new GraphicalVariablesView(m_dataDock);
    m_dataDock->setWidget(m_graphicalView);
	addDockWidget(Qt::BottomDockWidgetArea, m_dataDock);

	m_consoleDock = new QDockWidget(tr("Console"), this);
	m_consoleWidget = new ConsoleWidget(m_consoleDock);
	m_consoleDock->setWidget(m_consoleWidget);
	addDockWidget(Qt::BottomDockWidgetArea, m_consoleDock);
	tabifyDockWidget(m_dataDock, m_consoleDock);


	connect(m_stackView, &StackView::frameActivated, this,
	        [&](QString file, int line) {
		        showSourceLocation(file, line);
	        });

	m_breakDock = new QDockWidget(tr("Breakpoints"), this);
	m_breakView = new BreakpointsView(m_breakDock);
	m_breakDock->setWidget(m_breakView);
	addDockWidget(Qt::RightDockWidgetArea, m_breakDock);

	// Tabify with variables / stack:
	tabifyDockWidget(m_varsDock, m_breakDock);

	m_breakView->setSession(m_session.get());

	// Select a breakpoint → jump to editor
	connect(m_breakView, &BreakpointsView::breakpointSelected, this,
	        [&](QString loc) {
		        auto parts = loc.split(':');
		        if (parts.size() == 2) {
			        showSourceLocation(parts[0], parts[1].toInt());
		        }
	        });
}

SourceEditor* MainWindow::currentSourceEditor() const
{
	return m_sourceEditor;
}

void MainWindow::wireSourceEditor(SourceEditor* editor)
{
	if (!editor)
		return;
	editor->setSession(m_session.get());
	connect(editor, &SourceEditor::toggleBreakpointRequested,
	        this, &MainWindow::toggleBpAt);
	connect(editor, &SourceEditor::runUntilRequested,
	        this, [this](const QString& file, int line) {
		        m_session->runToCursor(QString("%1:%2").arg(file).arg(line));
	        });
}

SourceEditor* MainWindow::ensureSourceTabForFile(const QString& file)
{
	if (!m_sourceTabs)
		return currentSourceEditor();

	const QFileInfo fi(file);
	const QString canonical = fi.exists() ? fi.canonicalFilePath() : QString();
	const QString key = canonical.isEmpty() ? file : canonical;

	auto it = m_sourceEditorByFile.constFind(key);
	if (it != m_sourceEditorByFile.constEnd()) {
		if (it.value())
			return it.value();
		m_sourceEditorByFile.remove(key);
	}

	// Reuse the initial tab if it doesn't have an associated file yet.
	if (auto* existing = currentSourceEditor()) {
		const QString existingFile = existing->property("currentFile").toString();
		if (existingFile.isEmpty()) {
			m_sourceEditorByFile.insert(key, existing);
			return existing;
		}
	}

	auto* editor = new SourceEditor(m_sourceTabs);
	wireSourceEditor(editor);

	const QString title = fi.fileName().isEmpty() ? tr("Source") : fi.fileName();
	const int idx = m_sourceTabs->addTab(editor, title);
	m_sourceTabs->setTabToolTip(idx, fi.absoluteFilePath());
	m_sourceEditorByFile.insert(key, editor);
	return editor;
}

void MainWindow::showSourceLocation(const QString& file, int line)
{
	SourceEditor* editor = ensureSourceTabForFile(file);
	if (!editor)
		return;

	if (m_sourceTabs) {
		const int idx = m_sourceTabs->indexOf(editor);
		if (idx >= 0)
			m_sourceTabs->setCurrentIndex(idx);

		const QFileInfo fi(file);
		const QString title = fi.fileName().isEmpty() ? tr("Source") : fi.fileName();
		m_sourceTabs->setTabText(m_sourceTabs->currentIndex(), title);
		m_sourceTabs->setTabToolTip(m_sourceTabs->currentIndex(), fi.absoluteFilePath());
	}

	editor->showLocation(file, line);
	editor->setCurrentPC(line);
}

void MainWindow::setupMenusAndToolbars() {
	QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
	QAction *openAct = fileMenu->addAction(tr("Open Program..."));
	connect(openAct, &QAction::triggered, this, &MainWindow::openProgram);
	fileMenu->addSeparator();
	fileMenu->addAction(tr("Quit"), this, &QWidget::close);

	QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
	editMenu->addAction(tr("Copy"));
	editMenu->addAction(tr("Paste"));
	editMenu->addAction(tr("Find"));

	QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

	QAction *toggleConsole = viewMenu->addAction(tr("Console"));
	toggleConsole->setCheckable(true);
	toggleConsole->setChecked(true);
	connect(toggleConsole, &QAction::triggered, this,
	        [this] { m_consoleDock->setVisible(!m_consoleDock->isVisible()); });

	QAction *toggleData = viewMenu->addAction(tr("Data Display"));
	toggleData->setCheckable(true);
	toggleData->setChecked(true);
	connect(toggleData, &QAction::triggered, this,
	        [this] { m_dataDock->setVisible(!m_dataDock->isVisible()); });

	QAction *toggleStack = viewMenu->addAction(tr("Stack"));
	toggleStack->setCheckable(true);
	toggleStack->setChecked(true);
	connect(toggleStack, &QAction::triggered, this,
	        [this] { m_stackDock->setVisible(!m_stackDock->isVisible()); });

	QAction *toggleVars = viewMenu->addAction(tr("Variables"));
	toggleVars->setCheckable(true);
	toggleVars->setChecked(true);
	connect(toggleVars, &QAction::triggered, this,
	        [this] { m_varsDock->setVisible(!m_varsDock->isVisible()); });

	QMenu *programMenu = menuBar()->addMenu(tr("&Program"));

	QAction *runAct = programMenu->addAction(tr("Run"));
	runAct->setShortcut(Qt::Key_F2);
	QAction *runAgainAct = programMenu->addAction(tr("Run Again"));
	runAgainAct->setShortcut(Qt::Key_F3);
	QAction *contAct = programMenu->addAction(tr("Continue"));
	contAct->setShortcut(Qt::Key_F9);
	QAction *stepInAct = programMenu->addAction(tr("Step Into"));
	stepInAct->setShortcut(Qt::Key_F5);
	QAction *stepOverAct = programMenu->addAction(tr("Step Over"));
	stepOverAct->setShortcut(Qt::Key_F6);
	QAction *stepOutAct = programMenu->addAction(tr("Step Out"));
	QAction *interruptAct = programMenu->addAction(tr("Interrupt"));
	QAction *untilAct = programMenu->addAction(tr("Run Until Cursor"));
	untilAct->setShortcut(Qt::Key_F7);
	QAction *upAct = programMenu->addAction(tr("Up"));
	QAction *downAct = programMenu->addAction(tr("Down"));
	QAction *toggleBpAct = programMenu->addAction(tr("Toggle Breakpoint"));


	runAct->setIcon(QIcon(":/icons/resources/icons/run.svg"));
	contAct->setIcon(QIcon(":/icons/resources/icons/continue.svg"));
	stepInAct->setIcon(QIcon(":/icons/resources/icons/step-into.svg"));
	stepOverAct->setIcon(QIcon(":/icons/resources/icons/step-over.svg"));
	stepOutAct->setIcon(QIcon(":/icons/resources/icons/step-out.svg"));
	interruptAct->setIcon(QIcon(":/icons/resources/icons/stop.svg"));
	toggleBpAct->setIcon(QIcon(":/icons/resources/icons/toggle.svg"));
	untilAct->setIcon(QIcon(":/icons/resources/icons/run-cursor.svg"));

	connect(interruptAct, &QAction::triggered, this,
			[this] { m_session->interruptExecution(); });

	connect(untilAct, &QAction::triggered, this, [this] {
		QString file;
		int line;
		m_sourceEditor->currentLocation(file, line);
		m_session->runToCursor(QString("%1:%2").arg(file).arg(line));
	});

	connect(upAct, &QAction::triggered, this, [this] {
		int idx =
			m_stackView->currentFrameIndex();
		m_stackView->selectFrame(idx + 1);
	});

	connect(downAct, &QAction::triggered, this, [this] {
		int idx = m_stackView->currentFrameIndex();
		m_stackView->selectFrame(idx - 1);
	});
	connect(runAct, &QAction::triggered, this, &MainWindow::runProgram);
	connect(contAct, &QAction::triggered, this, &MainWindow::continueProgram);
	connect(stepInAct, &QAction::triggered, this, &MainWindow::stepInto);
	connect(stepOverAct, &QAction::triggered, this, &MainWindow::stepOver);
	connect(stepOutAct, &QAction::triggered, this, &MainWindow::stepOut);
	connect(upAct, &QAction::triggered, this, &MainWindow::up);
	connect(downAct, &QAction::triggered, this, &MainWindow::down);
	connect(toggleBpAct, &QAction::triggered, this, &MainWindow::toggleBp);

	QMenu *commandsMenu = menuBar()->addMenu(tr("&Commands"));
	commandsMenu->addAction(tr("Command History"));
	commandsMenu->addAction(tr("Previous"));
	commandsMenu->addAction(tr("Next"));
	commandsMenu->addAction(tr("Find Backward"));
	commandsMenu->addAction(tr("Find Forward"));
	commandsMenu->addAction(tr("Quit Search"));
	commandsMenu->addAction(tr("Complete"));
	commandsMenu->addAction(tr("Apply"));
	commandsMenu->addAction(tr("Clear Line"));
	commandsMenu->addAction(tr("Define Command"));
	commandsMenu->addAction(tr("Edit Buttons"));

	QMenu *statusMenu = menuBar()->addMenu(tr("&Status"));
	statusMenu->addAction(tr("Show PID"));
	statusMenu->addAction(tr("Show Signals"));

	QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
	helpMenu->addAction(tr("About"));

	QToolBar *dbgBar = new QToolBar(tr("Program Controls"), this);
	dbgBar->setMovable(true);
	dbgBar->setFloatable(true);
	dbgBar->setAllowedAreas(Qt::AllToolBarAreas);
	dbgBar->setIconSize(QSize(18, 18));
	dbgBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	dbgBar->setStyleSheet(R"(
		QToolBar {
			background: #1f1f1f;
			border-bottom: 1px solid #3c3c3c;
			padding: 0px;
			spacing: 0px;
		}
	)");

	auto* ribbon = new QWidget(dbgBar);
	ribbon->setObjectName("ribbon");
	ribbon->setStyleSheet(R"(
		#ribbon {
			background: #1f1f1f;
		}
		QToolButton {
			background: transparent;
			border: none;
			padding: 6px 8px;
			margin: 0px;
			color: #e6e6e6;
			font-size: 12px;
		}
		QToolButton:hover {
			background: #2a2a2a;
		}
		QToolButton:pressed {
			background: #151515;
		}
		QLabel#groupTitle {
			color: rgba(255,255,255,120);
			font-size: 11px;
			padding: 2px 0 0 0;
		}
		QFrame#groupFrame {
			background: transparent;
			padding: 6px 10px 4px 10px;
		}
		QFrame#groupSep {
			background: rgba(255,255,255,20);
			min-width: 1px;
			max-width: 1px;
			margin: 8px 6px;
		}
	)");

	auto* ribbonLayout = new QHBoxLayout(ribbon);
	ribbonLayout->setContentsMargins(8, 4, 8, 4);
	ribbonLayout->setSpacing(0);

	auto addGroup = [&](const QString& title, const QList<QAction*>& actions) {
		auto* frame = new QFrame(ribbon);
		frame->setObjectName("groupFrame");

		auto* v = new QVBoxLayout(frame);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(2);

		auto* row = new QHBoxLayout();
		row->setContentsMargins(0, 0, 0, 0);
		row->setSpacing(8);

		for (QAction* a : actions) {
			auto* b = new QToolButton(frame);
			b->setDefaultAction(a);
			b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
			b->setIconSize(QSize(18, 18));
			b->setAutoRaise(true);
			b->setMinimumHeight(30);
			row->addWidget(b);
		}

		v->addLayout(row);

		auto* l = new QLabel(title, frame);
		l->setObjectName("groupTitle");
		l->setAlignment(Qt::AlignHCenter);
		v->addWidget(l);

		ribbonLayout->addWidget(frame);

		auto* sep = new QFrame(ribbon);
		sep->setObjectName("groupSep");
		sep->setFrameShape(QFrame::VLine);
		ribbonLayout->addWidget(sep);
	};

	// Reverse flow control (best-effort)
	auto* stepBackAct = new QAction(tr("Step Back"), this);
	auto* nextBackAct = new QAction(tr("Next Back"), this);
	auto* contBackAct = new QAction(tr("Go Back"), this);
	stepBackAct->setIcon(QIcon(":/icons/resources/icons/step-back.svg"));
	nextBackAct->setIcon(QIcon(":/icons/resources/icons/next-back.svg"));
	contBackAct->setIcon(QIcon(":/icons/resources/icons/continue-back.svg"));
	stepBackAct->setEnabled(false);
	nextBackAct->setEnabled(false);
	contBackAct->setEnabled(false);
	connect(stepBackAct, &QAction::triggered, this, [this] { m_session->reverseStepInto(); });
	connect(nextBackAct, &QAction::triggered, this, [this] { m_session->reverseStepOver(); });
	connect(contBackAct, &QAction::triggered, this, [this] { m_session->reverseContinueExecution(); });

	addGroup(tr("Flow Control"),
	         {runAct, contAct, stepInAct, stepOverAct, stepOutAct, interruptAct});
	addGroup(tr("Reverse Flow Control"),
	         {stepBackAct, nextBackAct, contBackAct});
	addGroup(tr("Navigation"),
	         {untilAct, upAct, downAct, toggleBpAct});

	ribbonLayout->addStretch(1);

	auto updateReverseActions = [this, stepBackAct, nextBackAct, contBackAct] {
		const bool ok = m_session && m_session->supportsReverseExecution();
		stepBackAct->setEnabled(ok);
		nextBackAct->setEnabled(ok);
		contBackAct->setEnabled(ok);
		const QString tip = ok
			? tr("Reverse execution")
			: tr("Reverse execution is not available with the current debugger backend.");
		stepBackAct->setToolTip(tip);
		nextBackAct->setToolTip(tip);
		contBackAct->setToolTip(tip);
	};
	updateReverseActions();
	connect(m_session.get(), &DebuggerSession::targetStarted, this, updateReverseActions);
	connect(m_session.get(), &DebuggerSession::targetStopped, this, updateReverseActions);

	dbgBar->addWidget(ribbon);
	dbgBar->setMinimumHeight(64);
	addToolBar(Qt::TopToolBarArea, dbgBar);
}

void MainWindow::startDebugger(const QString &programPath) {
	m_currentProgram = programPath;
	m_breakOnMainInserted = false;
	m_session->startSession(programPath);
}

void MainWindow::openProgram() {
	QString file = QFileDialog::getOpenFileName(this, tr("Select executable"),
	                                            QDir::homePath());

	if (!file.isEmpty()) {
		startDebugger(file);
	}
}

void MainWindow::runProgram() {
	if (m_currentProgram.isEmpty())
		return;

	if (!m_breakOnMainInserted) {
		m_session->insertBreakpoint("main");
		m_breakOnMainInserted = true;
	}

	m_session->run();
}

void MainWindow::continueProgram() { m_session->continueExecution(); }

void MainWindow::stepInto() { m_session->stepInto(); }

void MainWindow::stepOver() { m_session->stepOver(); }

void MainWindow::stepOut() { m_session->stepOut(); }

void MainWindow::interrupt() { m_session->interruptExecution(); };


void MainWindow::up() {};

void MainWindow::down() {};

void MainWindow::toggleBp() {
	QString file;
	int line;
	if (auto* ed = currentSourceEditor())
		ed->currentLocation(file, line);

	if (file.isEmpty() || line <= 0)
		return;

	toggleBpAt(file, line);
}

void MainWindow::toggleBpAt(const QString& file, int line)
{
	if (file.isEmpty() || line <= 0)
		return;
	m_session->toggleBreakpoint(QString("%1:%2").arg(file).arg(line));
}


void MainWindow::onTargetStopped()
{
}
