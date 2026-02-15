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
		        Q_UNUSED(file);
		        m_sourceEditor->setBreakpointLines(lines);
	        });

	connect(m_session.get(), &DebuggerSession::snapshotCaptured, this,
	        [this](const ExecutionSnapshot &) { m_graphicalView->refresh(); });
	connect(m_session.get(), &DebuggerSession::variablesUpdated, m_graphicalView,
				  &GraphicalVariablesView::refresh);

	if (!m_currentProgram.isEmpty()) {
		startDebugger(m_currentProgram);
	}
}

void MainWindow::setupUi() {
	setDockOptions(QMainWindow::AllowTabbedDocks |
	               QMainWindow::AllowNestedDocks);

	m_sourceEditor = new SourceEditor(this);
	setCentralWidget(m_sourceEditor);

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
		        m_sourceEditor->setCurrentPC(line);
		        m_sourceEditor->showLocation(file, line);
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
			        m_sourceEditor->showLocation(parts[0], parts[1].toInt());
			        m_sourceEditor->setCurrentPC(parts[1].toInt());
		        }
	        });
	connect(m_sourceEditor, &SourceEditor::toggleBreakpointRequested,
			this, &MainWindow::toggleBp);
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
	dbgBar->setFixedHeight(32);
	dbgBar->setStyleSheet(R"(
    QToolBar {
        background: #252526;
        border-bottom: 1px solid #3c3c3c;
        padding: 6px 10px;
        spacing: 8px;
    }
    QToolButton {
        background: transparent;
        border: none;
        padding: 6px;
        margin: 0 2px;
        color: #0099dd;
    }
    QToolButton:hover {
        background: #333333;
    }
    QToolButton:pressed {
        background: #1e1e1e;
    }
	)");

	dbgBar->addAction(runAct);
	dbgBar->addAction(contAct);
	dbgBar->addAction(stepInAct);
	dbgBar->addAction(stepOverAct);
	dbgBar->addAction(stepOutAct);
	dbgBar->addAction(interruptAct);
	dbgBar->addAction(untilAct);
	dbgBar->addAction(upAct);
	dbgBar->addAction(downAct);
	dbgBar->addAction(toggleBpAct);

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
	m_sourceEditor->currentLocation(file, line);

	if (file.isEmpty() || line <= 0)
		return;

	m_session->toggleBreakpoint(QString("%1:%2").arg(file).arg(line));
}


void MainWindow::onTargetStopped()
{
}
