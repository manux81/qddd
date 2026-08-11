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

#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include <memory>

#include "BreakpointsView.h"
#include "ConsoleWidget.h"
#include "DebugSession.h"
#include "SourceEditor.h"
#include "StackView.h"
#include "VariablesView.h"

#include "GraphicalVariablesView.h"
#include "DisassemblyView.h"


class QDockWidget;
class QAction;
class QEvent;
class QResizeEvent;
class QTabWidget;
class QWidget;
class DebugAssistantDock;
class QComboBox;
class QLabel;
class QProgressBar;
class HardwareDebugSession;

class MainWindow : public QMainWindow {
	Q_OBJECT
  public:
	explicit MainWindow(const QString &initialProgram = QString(),
	                    QWidget *parent = nullptr);
	~MainWindow() override = default;

  protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

  private slots:
	void openProgram();
	void browseProjectSources();
	void openSettings();
	void selectGdbExecutable();
	void runProgram();
	void continueProgram();
	void stepInto();
	void stepOver();
	void stepOut();
	void interrupt();
	void up();
	void down();
	void toggleBp();
	void toggleBpAt(const QString& file, int line);

	void onTargetStopped();

  private:
	void setupUi();
	void setupMenusAndToolbars();
	void startDebugger(const QString &programPath);
	void applySettingsToSession();
	void refreshHardwareTargetSelector();
	void updateTargetChip(const QString& state);
	QString selectedHardwareProgram() const;
	SourceEditor* currentSourceEditor() const;
	SourceEditor* ensureSourceTabForFile(const QString& file);
	QString resolveSourceFile(const QString& file) const;
	void showSourceLocation(const QString& file, int line);
	void wireSourceEditor(SourceEditor* editor);
	void positionCommandOverlay();
	QString sourceProjectRoot() const;

	std::unique_ptr<DebuggerSession> m_session;
	std::unique_ptr<HardwareDebugSession> m_hardwareSession;
	QComboBox* m_targetSelector = nullptr;
	QLabel* m_targetState = nullptr;
	QLabel* m_downloadStatus = nullptr;
	QProgressBar* m_downloadProgress = nullptr;

	QTabWidget *m_sourceTabs = nullptr;
	SourceEditor *m_sourceEditor = nullptr;
	QHash<QString, QPointer<SourceEditor>> m_sourceEditorByFile;
	VariablesView *m_variablesView = nullptr;
	StackView *m_stackView = nullptr;
    GraphicalVariablesView *m_graphicalView = nullptr;
	ConsoleWidget *m_consoleWidget = nullptr;
	BreakpointsView *m_breakView = nullptr;

	QDockWidget *m_varsDock = nullptr;
	QDockWidget *m_stackDock = nullptr;
	QDockWidget *m_dataDock = nullptr;
	QDockWidget *m_consoleDock = nullptr;
	QDockWidget *m_breakDock = nullptr;
	QDockWidget *m_aiDock = nullptr;
	QDockWidget *m_disasmDock = nullptr;
	DebugAssistantDock *m_aiAssistant = nullptr;
	DisassemblyView *m_disasmView = nullptr;
	QWidget *m_commandOverlay = nullptr;

	QString m_currentProgram;
	bool m_breakOnMainInserted = false;
	int m_activeTargetId = -4;
	bool m_runAfterSessionStart = false;
	bool m_hardwareAutoRuns = false;
};
