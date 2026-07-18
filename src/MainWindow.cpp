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
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMenuBar>
#include <QMessageBox>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QTabWidget>
#include <QSettings>
#include <QComboBox>

#include "SettingsDialog.h"
#include "DebugAssistantDock.h"
#include "HardwareDebugSession.h"
#include "HardwareServerConfig.h"

namespace {
constexpr int kAutomaticTarget = -3;
constexpr int kExternalServerTarget = -2;
constexpr int kLocalTarget = -1;
}

MainWindow::MainWindow(const QString &initialProgram, QWidget *parent)
    : QMainWindow(parent), m_session(std::make_unique<DebuggerSession>(this)),
      m_hardwareSession(std::make_unique<HardwareDebugSession>(this)),
      m_currentProgram(initialProgram) {
	setupUi();
	setupMenusAndToolbars();
	refreshHardwareTargetSelector();

	applySettingsToSession();

	m_consoleWidget->setSession(m_session.get());
	m_stackView->setSession(m_session.get());
	m_variablesView->setSession(m_session.get());
	m_graphicalView->setSession(m_session.get());
	if (m_disasmView)
		m_disasmView->setSession(m_session.get());

	connect(m_session.get(), &DebuggerSession::targetStopped, this,
	        &MainWindow::onTargetStopped);
	connect(m_hardwareSession.get(), &HardwareDebugSession::debugOutput,
	        m_consoleWidget, &ConsoleWidget::appendOutput);
	connect(m_hardwareSession.get(), &HardwareDebugSession::serverOutput,
	        m_consoleWidget, &ConsoleWidget::appendOutput);
	connect(m_hardwareSession.get(), &HardwareDebugSession::sessionError, this,
	        [this](const QString& message) {
		        m_runAfterSessionStart = false;
		        QMessageBox::critical(this, tr("Hardware debug"), message);
	        });
	connect(m_session.get(), &DebuggerSession::targetStarted, this, [this] {
		if (m_runAfterSessionStart && m_activeTargetId < 0) {
			m_runAfterSessionStart = false;
			runProgram();
		}
	});
	connect(m_hardwareSession.get(), &HardwareDebugSession::sessionStarted, this, [this] {
		if (!m_runAfterSessionStart || m_activeTargetId < 0)
			return;
		m_runAfterSessionStart = false;
		if (!m_hardwareAutoRuns)
			runProgram();
	});

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
	        [this](const QString& file, int line, const QString& function) {
		        showSourceLocation(file, line);
		        if (m_aiAssistant)
			        m_aiAssistant->setLastStopLocation(file, line, function);
	        });

	if (!m_currentProgram.isEmpty()) {
		// A program supplied on the command line must retain the pre-selector
		// launch semantics, regardless of the last manual hardware choice.
		const int automaticIndex = m_targetSelector->findData(kAutomaticTarget);
		if (automaticIndex >= 0) {
			m_targetSelector->blockSignals(true);
			m_targetSelector->setCurrentIndex(automaticIndex);
			m_targetSelector->blockSignals(false);
		}
		startDebugger(m_currentProgram);
	}
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
	QMainWindow::resizeEvent(event);
	positionCommandOverlay();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_sourceTabs &&
	    (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
		positionCommandOverlay();
	}

	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::positionCommandOverlay()
{
	if (!m_commandOverlay)
		return;

	if (m_sourceTabs) {
		const int bottomMargin = 10;
		const int y = qMax(0, m_sourceTabs->height() - m_commandOverlay->height() - bottomMargin);
		m_commandOverlay->setGeometry(0, y, m_sourceTabs->width(), m_commandOverlay->height());
	} else {
		m_commandOverlay->setGeometry(0, height() - m_commandOverlay->height() - 10,
		                              width(), m_commandOverlay->height());
	}
	m_commandOverlay->raise();
}

void MainWindow::setupUi() {
	setDockOptions(QMainWindow::AllowTabbedDocks |
	               QMainWindow::AllowNestedDocks);
	setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

	m_sourceTabs = new QTabWidget(this);
	m_sourceTabs->setDocumentMode(true);
	m_sourceTabs->setMovable(true);
	m_sourceTabs->setTabsClosable(false);
	m_sourceTabs->installEventFilter(this);
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

	m_disasmDock = new QDockWidget(tr("Disassembly"), this);
	m_disasmView = new DisassemblyView(m_disasmDock);
	m_disasmDock->setWidget(m_disasmView);
	addDockWidget(Qt::RightDockWidgetArea, m_disasmDock);
	tabifyDockWidget(m_stackDock, m_disasmDock);
	m_disasmDock->setVisible(false);
	m_disasmView->setAutoRefreshEnabled(false);


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

	// AI Debug Assistant
	m_aiDock = new QDockWidget(tr("Debug Assistant"), this);
	m_aiAssistant = new DebugAssistantDock(m_session.get(), m_aiDock);
	m_aiDock->setWidget(m_aiAssistant);
	addDockWidget(Qt::RightDockWidgetArea, m_aiDock);
	tabifyDockWidget(m_varsDock, m_aiDock);

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
	QAction *settingsAct = fileMenu->addAction(tr("Settings..."));
	settingsAct->setShortcut(QKeySequence::Preferences);
	connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);
	QAction *selectGdbAct = fileMenu->addAction(tr("Select GDB..."));
	connect(selectGdbAct, &QAction::triggered, this, &MainWindow::selectGdbExecutable);
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

	QAction *toggleDisasm = viewMenu->addAction(tr("Disassembly"));
	toggleDisasm->setCheckable(true);
	toggleDisasm->setChecked(false);
	connect(toggleDisasm, &QAction::toggled, this,
	        [this](bool on) {
		        if (m_disasmDock)
			        m_disasmDock->setVisible(on);
		        if (m_disasmView)
			        m_disasmView->setAutoRefreshEnabled(on);
	        });

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
	toggleBpAct->setIcon(QIcon(":/icons/resources/icons/breakpoint-on.svg"));
	untilAct->setIcon(QIcon(":/icons/resources/icons/run-cursor.svg"));
	upAct->setIcon(QIcon(":/icons/resources/icons/stack-up.svg"));
	downAct->setIcon(QIcon(":/icons/resources/icons/stack-down.svg"));

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

	auto* toolbarWrap = new QWidget(m_sourceTabs ? static_cast<QWidget*>(m_sourceTabs) : this);
	toolbarWrap->setObjectName("toolbarWrap");
	toolbarWrap->setStyleSheet(R"(
		#toolbarWrap {
			background: transparent;
		}
		QFrame#commandPill {
			background: #252526;
			border: 1px solid #3a3a3a;
			border-radius: 18px;
		}
		QToolButton {
			background: transparent;
			border: none;
			border-radius: 13px;
			padding: 4px;
			margin: 0px;
			color: #d8d8d8;
			min-width: 26px;
			min-height: 26px;
			max-width: 26px;
			max-height: 26px;
		}
		QToolButton:hover {
			background: #333333;
		}
		QToolButton:pressed {
			background: #1b1b1b;
		}
		QToolButton:disabled {
			color: #5f5f5f;
			background: transparent;
		}
		QToolButton#primaryCommand {
			background: #2f3f33;
			color: #d8f3dc;
		}
		QToolButton#primaryCommand:hover {
			background: #3b5141;
		}
		QToolButton#dangerCommand:hover {
			background: #4a2a2a;
		}
		QFrame#commandSep {
			background: #3a3a3a;
			min-width: 1px;
			max-width: 1px;
			min-height: 24px;
			max-height: 24px;
			margin: 0px 6px;
		}
		QLabel#targetName {
			background: transparent;
			color: #dcdcdc;
			font-size: 12px;
			font-weight: 600;
			padding-left: 8px;
		}
		QLabel#targetState {
			background: transparent;
			color: #9a9a9a;
			font-size: 11px;
			padding-left: 8px;
		}
		QComboBox#targetSelector {
			background: transparent;
			border: none;
			border-right: 1px solid #3a3a3a;
			padding: 3px 18px 3px 6px;
			color: #dcdcdc;
			font-size: 12px;
			font-weight: 600;
		}
		QComboBox#targetSelector:hover { background: #333333; border-radius: 4px; }
		QComboBox#targetSelector::drop-down { border: none; width: 18px; }
	)");

	auto* wrapLayout = new QHBoxLayout(toolbarWrap);
	wrapLayout->setContentsMargins(0, 0, 0, 0);
	wrapLayout->setSpacing(0);
	wrapLayout->addStretch(1);

	auto* commandPill = new QFrame(toolbarWrap);
	commandPill->setObjectName("commandPill");
	auto* pillLayout = new QHBoxLayout(commandPill);
	pillLayout->setContentsMargins(8, 4, 8, 4);
	pillLayout->setSpacing(2);

	auto addSeparator = [&] {
		auto* sep = new QFrame(commandPill);
		sep->setObjectName("commandSep");
		sep->setFrameShape(QFrame::VLine);
		pillLayout->addWidget(sep);
	};

	auto addButton = [&](QAction* action, const char* objectName = nullptr) {
		auto* button = new QToolButton(commandPill);
		button->setDefaultAction(action);
		button->setToolButtonStyle(Qt::ToolButtonIconOnly);
		button->setIconSize(QSize(16, 16));
		button->setAutoRaise(true);
		if (objectName)
			button->setObjectName(objectName);
		pillLayout->addWidget(button);
		return button;
	};

	runAct->setToolTip(tr("Run (F2)"));
	contAct->setToolTip(tr("Continue (F9)"));
	stepInAct->setToolTip(tr("Step Into (F5)"));
	stepOverAct->setToolTip(tr("Step Over (F6)"));
	stepOutAct->setToolTip(tr("Step Out"));
	interruptAct->setToolTip(tr("Interrupt"));
	untilAct->setToolTip(tr("Run Until Cursor (F7)"));
	upAct->setToolTip(tr("Up Stack Frame"));
	downAct->setToolTip(tr("Down Stack Frame"));
	toggleBpAct->setToolTip(tr("Toggle Breakpoint"));

	auto* targetState = new QLabel(commandPill);
	targetState->setObjectName("targetState");
	m_targetSelector = new QComboBox(commandPill);
	m_targetSelector->setObjectName("targetSelector");
	m_targetSelector->setMinimumWidth(105);
	m_targetSelector->setMaximumWidth(160);
	m_targetSelector->setToolTip(tr("Debug target / hardware probe"));
	connect(m_targetSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, [this](int) {
		        QSettings settings;
		        settings.setValue("hardwareDebug/selectedIndex",
		                          m_targetSelector->currentData().toInt());
	        });
	pillLayout->addWidget(m_targetSelector);
	pillLayout->addWidget(targetState);
	addSeparator();
	addButton(runAct, "primaryCommand");
	addButton(contAct);
	addButton(interruptAct, "dangerCommand");
	addSeparator();
	addButton(stepInAct);
	addButton(stepOverAct);
	addButton(stepOutAct);
	addSeparator();
	addButton(untilAct);
	addButton(upAct);
	addButton(downAct);
	addButton(toggleBpAct);

	wrapLayout->addWidget(commandPill, 0, Qt::AlignCenter);
	wrapLayout->addStretch(1);

	auto updateTargetChip = [this, targetState](const QString& state) {
		const QFileInfo fi(m_currentProgram);
		const QString program = fi.exists() ? fi.fileName() : tr("No firmware");
		targetState->setText(QStringLiteral("%1 · %2").arg(program, state));
	};
	updateTargetChip(m_currentProgram.isEmpty() ? tr("Select firmware") : tr("Ready"));
	connect(m_session.get(), &DebuggerSession::targetStarted, this,
	        [updateTargetChip] { updateTargetChip(QObject::tr("Session started")); });
	connect(m_session.get(), &DebuggerSession::targetRunning, this,
	        [updateTargetChip] { updateTargetChip(QObject::tr("Running")); });
	connect(m_session.get(), &DebuggerSession::targetStopped, this,
	        [updateTargetChip] { updateTargetChip(QObject::tr("Stopped")); });
	connect(m_session.get(), &DebuggerSession::targetExited, this,
	        [updateTargetChip](int code) { updateTargetChip(QObject::tr("Exited (%1)").arg(code)); });

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

	addSeparator();
	addButton(stepBackAct);
	addButton(nextBackAct);
	addButton(contBackAct);

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

	m_commandOverlay = toolbarWrap;
	m_commandOverlay->setFixedHeight(44);
	positionCommandOverlay();
	m_commandOverlay->raise();
	m_commandOverlay->show();
}

void MainWindow::startDebugger(const QString &programPath) {
	m_currentProgram = programPath;
	m_breakOnMainInserted = false;
	if (m_hardwareSession)
		m_hardwareSession->stopSession();
	else if (m_session->isRunning())
		m_session->terminateSession();

	const int configIndex = m_targetSelector
		? m_targetSelector->currentData().toInt()
		: -1;
	if (configIndex < 0) {
		applySettingsToSession();
		if (configIndex == kLocalTarget)
			m_session->setTargetType(DebuggerSession::TargetType::Local);
		else if (configIndex == kExternalServerTarget)
			m_session->setTargetType(DebuggerSession::TargetType::RemoteGdbserver);
		// Automatic deliberately keeps the legacy target/type setting applied
		// above, preserving command-line and existing project workflows.
		m_activeTargetId = configIndex;
		m_hardwareAutoRuns = false;
		m_session->startSession(programPath);
		return;
	}

	QSettings settings;
	HardwareDebugConfigManager manager;
	manager.load(settings);
	if (configIndex >= manager.configurations.size()) {
		QMessageBox::warning(this, tr("Hardware debug"),
		                     tr("The selected hardware configuration no longer exists."));
		refreshHardwareTargetSelector();
		return;
	}

	HardwareDebugConfiguration config = manager.configurations[configIndex];
	config.programImage = programPath;
	if (config.symbolFile.isEmpty())
		config.symbolFile = programPath;
	m_activeTargetId = configIndex;
	m_hardwareAutoRuns = config.runAfterLoad;
	m_hardwareSession->startSession(config, m_session.get());
}

void MainWindow::openProgram() {
	QString file = QFileDialog::getOpenFileName(this, tr("Select executable"),
	                                            QDir::homePath());

	if (!file.isEmpty()) {
		startDebugger(file);
	}
}

void MainWindow::openSettings()
{
	SettingsDialog dlg(this);
	const int result = dlg.exec();
	applySettingsToSession();
	refreshHardwareTargetSelector();
	if (result != QDialog::Accepted)
		return;
}

void MainWindow::refreshHardwareTargetSelector()
{
	if (!m_targetSelector)
		return;

	QSettings settings;
	const int previous = settings.value("hardwareDebug/selectedIndex", kAutomaticTarget).toInt();
	HardwareDebugConfigManager manager;
	manager.load(settings);

	m_targetSelector->blockSignals(true);
	m_targetSelector->clear();
	m_targetSelector->addItem(tr("Automatic"), kAutomaticTarget);
	m_targetSelector->setItemData(0,
		tr("Use the legacy launch configuration; this is the compatibility default for firmware passed on the command line"),
		Qt::ToolTipRole);
	m_targetSelector->addItem(tr("Local"), kLocalTarget);
	m_targetSelector->setItemData(1, tr("Debug a local executable"), Qt::ToolTipRole);
	m_targetSelector->addItem(tr("External server"), kExternalServerTarget);
	m_targetSelector->setItemData(2, tr("Connect to the GDB server configured in Settings"), Qt::ToolTipRole);
	for (int i = 0; i < manager.configurations.size(); ++i) {
		const auto& config = manager.configurations[i];
		if (config.enabled)
			m_targetSelector->addItem(config.name, i);
	}
	int selected = m_targetSelector->findData(previous);
	if (selected < 0 && manager.activeIndex >= 0)
		selected = m_targetSelector->findData(manager.activeIndex);
	m_targetSelector->setCurrentIndex(selected >= 0 ? selected : 0);
	m_targetSelector->blockSignals(false);
}

void MainWindow::selectGdbExecutable()
{
	const QString file = QFileDialog::getOpenFileName(this, tr("Select GDB executable"), QDir::homePath());
	if (file.isEmpty())
		return;

	QSettings s;
	s.setValue("debugger/gdbPath", file);
	applySettingsToSession();
}

void MainWindow::applySettingsToSession()
{
	QSettings s;
	const QString backend = s.value("debugger/backend", "gdb-mi").toString();
	if (backend == "lldb-mi")
		m_session->setBackend(DebuggerSession::Backend::LldbMi);
	else
		m_session->setBackend(DebuggerSession::Backend::GdbMi);

	m_session->setGdbExecutable(s.value("debugger/gdbPath", "gdb").toString());
	m_session->setLldbMiExecutable(s.value("debugger/lldbMiPath", "/usr/local/bin/lldb-mi").toString());

	const QString targetType = s.value("target/type", "local").toString();
	if (targetType == "gdbserver")
		m_session->setTargetType(DebuggerSession::TargetType::RemoteGdbserver);
	else if (targetType == "jlink")
		m_session->setTargetType(DebuggerSession::TargetType::JLink);
	else if (targetType == "stlink")
		m_session->setTargetType(DebuggerSession::TargetType::Stlink);
	else
		m_session->setTargetType(DebuggerSession::TargetType::Local);

	m_session->setRemoteEndpoint(
		s.value("target/remoteHost", "127.0.0.1").toString(),
		s.value("target/remotePort", 3333).toInt());
	m_session->setRemoteConnectCommands({}, false);

	if (m_aiDock)
		m_aiDock->setVisible(s.value("ai/enabled", true).toBool());
}

void MainWindow::runProgram() {
	if (m_currentProgram.isEmpty())
		return;

	const int selectedTarget = m_targetSelector
		? m_targetSelector->currentData().toInt()
		: kAutomaticTarget;
	if (selectedTarget != m_activeTargetId || !m_session->isRunning()) {
		m_runAfterSessionStart = true;
		startDebugger(m_currentProgram);
		return;
	}

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
