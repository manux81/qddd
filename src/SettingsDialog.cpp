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

#include "SettingsDialog.h"
#include "HardwareDebugPage.h"
#include "ToggleSwitch.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

constexpr const char* kKeyBackend      = "debugger/backend";
constexpr const char* kKeyGdbPath      = "debugger/gdbPath";
constexpr const char* kKeyLldbMiPath   = "debugger/lldbMiPath";
constexpr const char* kKeyReverseMode  = "debugger/reverseMode";
constexpr const char* kKeyTargetType         = "target/type";
constexpr const char* kKeyRemoteHost         = "target/remoteHost";
constexpr const char* kKeyRemotePort         = "target/remotePort";
constexpr const char* kKeyOpenAIKey    = "ai/openaiApiKey";
constexpr const char* kKeyOpenAIModel  = "ai/openaiModel";
constexpr const char* kKeyOpenAIBaseUrl= "ai/openaiBaseUrl";
constexpr const char* kKeyAIEnabled    = "ai/enabled";
constexpr const char* kKeyAIProvider   = "ai/provider";
constexpr const char* kKeyOllamaModel  = "ai/ollamaModel";
constexpr const char* kKeyOllamaBaseUrl= "ai/ollamaBaseUrl";

QString withBrowse(QLineEdit* edit, const QString& caption, QWidget* parent,
                   const QString& filter = QString())
{
	const QString start = edit && !edit->text().isEmpty()
		? edit->text()
		: QString();
	return QFileDialog::getOpenFileName(parent, caption, start, filter);
}

QLabel* makeSectionTitle(const QString& text, QWidget* parent)
{
	auto* label = new QLabel(text, parent);
	label->setObjectName(QStringLiteral("settingsSectionTitle"));
	return label;
}

QWidget* makeSettingsRow(const QString& title, const QString& description,
	                     QWidget* control, QWidget* parent, int controlWidth = 430)
{
	auto* row = new QWidget(parent);
	row->setObjectName(QStringLiteral("settingsRow"));
	auto* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 8, 0, 8);
	layout->setSpacing(28);

	auto* copy = new QWidget(row);
	auto* copyLayout = new QVBoxLayout(copy);
	copyLayout->setContentsMargins(0, 0, 0, 0);
	copyLayout->setSpacing(4);
	auto* titleLabel = new QLabel(title, copy);
	titleLabel->setObjectName(QStringLiteral("settingsRowTitle"));
	copyLayout->addWidget(titleLabel);
	if (!description.isEmpty()) {
		auto* descriptionLabel = new QLabel(description, copy);
		descriptionLabel->setObjectName(QStringLiteral("settingsRowDescription"));
		descriptionLabel->setWordWrap(true);
		copyLayout->addWidget(descriptionLabel);
	}

	if (controlWidth > 0) {
		control->setMinimumWidth(qMin(320, controlWidth));
		control->setMaximumWidth(controlWidth);
	}
	layout->addWidget(copy, 1);
	layout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
	return row;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
	: QDialog(parent)
{
	setObjectName(QStringLiteral("settingsDialog"));
	setWindowTitle(tr("Settings"));
	setModal(true);
	resize(980, 720);
	setMinimumSize(820, 600);
	setStyleSheet(QStringLiteral(R"(
		QDialog#settingsDialog {
			background: #1e1e1e;
			color: #cccccc;
		}
		QDialog#settingsDialog QWidget {
			background: #1e1e1e;
			color: #cccccc;
		}
		QDialog#settingsDialog QFrame#settingsHeader,
		QDialog#settingsDialog QFrame#settingsFooter {
			background: #1e1e1e;
		}
		QDialog#settingsDialog QFrame#settingsFooter {
			border-top: 1px solid #3c3c3c;
		}
		QDialog#settingsDialog QLabel#settingsTitle {
			font-size: 34px;
			font-weight: 700;
			color: #ffffff;
		}
		QDialog#settingsDialog QLabel#settingsSubtitle {
			font-size: 13px;
			color: #a8a8a8;
		}
		QDialog#settingsDialog QTabWidget#settingsTabs::pane {
			background: #1e1e1e;
			border: 0;
		}
		QDialog#settingsDialog QTabWidget#settingsTabs::tab-bar {
			left: 38px;
		}
		QDialog#settingsDialog QTabWidget#settingsTabs {
			background: #1e1e1e;
		}
		QDialog#settingsDialog QTabWidget#settingsTabs QTabBar {
			background: #252526;
		}
		QDialog#settingsDialog QTabWidget#settingsTabs QTabBar::tab {
			min-width: 124px;
			min-height: 42px;
			padding: 0 14px;
			margin: 0 8px 0 0;
			background: transparent;
			border: 0;
			border-bottom: 3px solid transparent;
			color: #b3b3b3;
			font-size: 13px;
			font-weight: 600;
		}
		QDialog#settingsDialog QTabWidget#settingsTabs QTabBar::tab:hover {
			color: #ffffff;
		}
		QDialog#settingsDialog QTabWidget#settingsTabs QTabBar::tab:selected {
			background: transparent;
			border-bottom: 3px solid #007acc;
			color: #ffffff;
		}
		QDialog#settingsDialog QScrollArea,
		QDialog#settingsDialog QScrollArea > QWidget > QWidget {
			background: #1e1e1e;
			border: 0;
		}
		QDialog#settingsDialog QGroupBox {
			background: transparent;
			border: 0;
			margin-top: 28px;
			padding-top: 22px;
			font-size: 18px;
			font-weight: 700;
			color: #ffffff;
		}
		QDialog#settingsDialog QGroupBox::title {
			subcontrol-origin: margin;
			subcontrol-position: top left;
			left: 0;
			padding: 0;
		}
		QDialog#settingsDialog QLabel {
			background: transparent;
			color: #b3b3b3;
			font-size: 13px;
		}
		QDialog#settingsDialog QLabel#introCard {
			background: #252526;
			border-radius: 8px;
			padding: 14px 16px;
			color: #b3b3b3;
		}
		QDialog#settingsDialog QLabel#settingsSectionTitle {
			font-size: 20px;
			font-weight: 700;
			color: #ffffff;
			padding-top: 18px;
			padding-bottom: 5px;
		}
		QDialog#settingsDialog QLabel#settingsRowTitle {
			font-size: 14px;
			font-weight: 650;
			color: #ffffff;
		}
		QDialog#settingsDialog QLabel#settingsRowDescription {
			font-size: 12px;
			color: #9d9d9d;
		}
		QDialog#settingsDialog QLineEdit,
		QDialog#settingsDialog QComboBox,
		QDialog#settingsDialog QSpinBox,
		QDialog#settingsDialog QPlainTextEdit,
		QDialog#settingsDialog QListWidget {
			background: #2d2d2d;
			border: 1px solid #3c3c3c;
			border-radius: 7px;
			padding: 8px 12px;
			selection-background-color: #094771;
			selection-color: #ffffff;
			color: #e6e6e6;
			font-size: 13px;
		}
		QDialog#settingsDialog QLineEdit,
		QDialog#settingsDialog QComboBox,
		QDialog#settingsDialog QSpinBox {
			min-height: 26px;
		}
		QDialog#settingsDialog QLineEdit:focus,
		QDialog#settingsDialog QComboBox:focus,
		QDialog#settingsDialog QSpinBox:focus,
		QDialog#settingsDialog QPlainTextEdit:focus,
		QDialog#settingsDialog QListWidget:focus {
			border: 1px solid #007acc;
		}
		QDialog#settingsDialog QLineEdit:disabled,
		QDialog#settingsDialog QComboBox:disabled {
			background: #252526;
			color: #666666;
		}
		QDialog#settingsDialog QComboBox::drop-down {
			width: 30px;
			border: 0;
		}
		QDialog#settingsDialog QComboBox QAbstractItemView {
			background: #252526;
			border: 1px solid #3c3c3c;
			selection-background-color: #094771;
			color: #ffffff;
			outline: 0;
		}
		QDialog#settingsDialog QPushButton {
			min-height: 34px;
			padding: 0 16px;
			background: transparent;
			border: 1px solid #5a5a5a;
			border-radius: 18px;
			color: #ffffff;
			font-size: 13px;
			font-weight: 600;
		}
		QDialog#settingsDialog QPushButton:hover {
			border-color: #007acc;
			background: #2d2d2d;
		}
		QDialog#settingsDialog QPushButton:pressed {
			background: #094771;
		}
		QDialog#settingsDialog QDialogButtonBox QPushButton {
			min-width: 82px;
		}
		QDialog#settingsDialog QPushButton#primaryButton {
			background: #007acc;
			border-color: #007acc;
			color: #ffffff;
		}
		QDialog#settingsDialog QPushButton#primaryButton:hover {
			background: #118edb;
			border-color: #118edb;
		}
		QDialog#settingsDialog QCheckBox {
			spacing: 12px;
			color: #e6e6e6;
			font-size: 13px;
		}
		QDialog#settingsDialog QScrollBar:vertical {
			background: transparent;
			width: 12px;
			margin: 4px 2px;
		}
		QDialog#settingsDialog QScrollBar::handle:vertical {
			background: #cbd5e1;
			border-radius: 5px;
			min-height: 40px;
		}
		QDialog#settingsDialog QScrollBar::handle:vertical:hover {
			background: #94a3b8;
		}
		QDialog#settingsDialog QScrollBar::add-line:vertical,
		QDialog#settingsDialog QScrollBar::sub-line:vertical {
			height: 0;
		}
		QDialog#settingsDialog QScrollBar::add-page:vertical,
		QDialog#settingsDialog QScrollBar::sub-page:vertical {
			background: transparent;
		}
	)"));

	auto* tabs = new QTabWidget(this);
	tabs->setObjectName(QStringLiteral("settingsTabs"));
	tabs->setDocumentMode(true);
	tabs->tabBar()->setExpanding(false);
	tabs->tabBar()->setUsesScrollButtons(false);

	// --- Debugger tab ---
	auto* debuggerPage = new QWidget(tabs);
	auto* debuggerPageLayout = new QVBoxLayout(debuggerPage);
	debuggerPageLayout->setContentsMargins(0, 0, 0, 0);
	auto* debuggerScroll = new QScrollArea(debuggerPage);
	debuggerScroll->setWidgetResizable(true);
	debuggerScroll->setFrameShape(QFrame::NoFrame);
	auto* debuggerContent = new QWidget(debuggerScroll);
	auto* dbgLayout = new QVBoxLayout(debuggerContent);
	dbgLayout->setContentsMargins(38, 16, 38, 34);
	dbgLayout->setSpacing(12);
	dbgLayout->addWidget(makeSectionTitle(tr("Debugger backend"), debuggerContent));

	m_backendCombo = new QComboBox(debuggerContent);
	m_backendCombo->addItem(tr("GDB (MI2)"), "gdb-mi");
	m_backendCombo->addItem(tr("LLDB (lldb-mi / MI3)"), "lldb-mi");
	dbgLayout->addWidget(makeSettingsRow(
		tr("Backend"),
		tr("Choose the debugger engine used for new sessions."),
		m_backendCombo, debuggerContent));

	auto* gdbRow = new QWidget(debuggerContent);
	auto* gdbRowLayout = new QHBoxLayout(gdbRow);
	gdbRowLayout->setContentsMargins(0, 0, 0, 0);
	m_gdbPathEdit = new QLineEdit(gdbRow);
	auto* gdbBrowse = new QPushButton(tr("Browse..."), gdbRow);
	connect(gdbBrowse, &QPushButton::clicked, this, &SettingsDialog::browseGdb);
	gdbRowLayout->addWidget(m_gdbPathEdit, 1);
	gdbRowLayout->addWidget(gdbBrowse);
	dbgLayout->addWidget(makeSettingsRow(
		tr("GDB executable"),
		tr("Path to the GDB executable used by the MI backend."),
		gdbRow, debuggerContent));

	auto* lldbRow = new QWidget(debuggerContent);
	auto* lldbRowLayout = new QHBoxLayout(lldbRow);
	lldbRowLayout->setContentsMargins(0, 0, 0, 0);
	m_lldbMiPathEdit = new QLineEdit(lldbRow);
	auto* lldbBrowse = new QPushButton(tr("Browse..."), lldbRow);
	connect(lldbBrowse, &QPushButton::clicked, this, &SettingsDialog::browseLldbMi);
	lldbRowLayout->addWidget(m_lldbMiPathEdit, 1);
	lldbRowLayout->addWidget(lldbBrowse);
	dbgLayout->addWidget(makeSettingsRow(
		tr("LLDB-MI executable"),
		tr("Path to lldb-mi when the LLDB backend is selected."),
		lldbRow, debuggerContent));

	m_reverseModeCombo = new QComboBox(debuggerContent);
	m_reverseModeCombo->addItem(tr("Auto (safe branch tracing)"), "auto");
	m_reverseModeCombo->addItem(tr("Branch trace (recommended)"), "btrace");
	m_reverseModeCombo->addItem(tr("Full record (experimental)"), "full");
	m_reverseModeCombo->addItem(tr("Disabled"), "disabled");
	dbgLayout->addWidget(makeSettingsRow(
		tr("Reverse execution"),
		tr("Auto and branch trace use safe hardware-assisted recording. Full record is experimental."),
		m_reverseModeCombo, debuggerContent));

	// --- External target section (classic remote gdbserver / local) ---
	dbgLayout->addWidget(makeSectionTitle(tr("External GDB server"), debuggerContent));

	m_targetTypeCombo = new QComboBox(debuggerContent);
	m_targetTypeCombo->addItem(tr("Local process"), "local");
	m_targetTypeCombo->addItem(tr("External GDB server"), "gdbserver");
	m_targetTypeCombo->hide(); // target mode is selected explicitly in the main window

	m_remoteHostEdit = new QLineEdit(debuggerContent);
	m_remoteHostEdit->setPlaceholderText("127.0.0.1");
	dbgLayout->addWidget(makeSettingsRow(
		tr("Remote host"),
		tr("Address of an already running GDB server."),
		m_remoteHostEdit, debuggerContent));

	m_remotePortSpin = new QSpinBox(debuggerContent);
	m_remotePortSpin->setRange(1, 65535);
	m_remotePortSpin->setValue(3333);
	dbgLayout->addWidget(makeSettingsRow(
		tr("Remote port"),
		tr("For managed ST-LINK, J-Link or OpenOCD targets, use a Hardware probe profile instead."),
		m_remotePortSpin, debuggerContent));
	dbgLayout->addStretch(1);
	debuggerScroll->setWidget(debuggerContent);
	debuggerPageLayout->addWidget(debuggerScroll);

	tabs->addTab(debuggerPage, tr("Debugger"));

	// --- AI tab ---
	auto* aiPage = new QWidget(tabs);
	auto* aiPageLayout = new QVBoxLayout(aiPage);
	aiPageLayout->setContentsMargins(0, 0, 0, 0);
	auto* aiScroll = new QScrollArea(aiPage);
	aiScroll->setWidgetResizable(true);
	aiScroll->setFrameShape(QFrame::NoFrame);
	auto* aiContent = new QWidget(aiScroll);
	auto* aiLayout = new QVBoxLayout(aiContent);
	aiLayout->setContentsMargins(38, 16, 38, 34);
	aiLayout->setSpacing(12);

	aiLayout->addWidget(makeSectionTitle(tr("Debug Assistant"), aiContent));

	m_aiEnabledCheck = new ToggleSwitch(QString(), aiContent);
	aiLayout->addWidget(makeSettingsRow(
		tr("Enable Debug Assistant"),
		tr("Allow QDDD to analyse the current debug session and suggest actions."),
		m_aiEnabledCheck, aiContent, 0));

	m_aiProviderCombo = new QComboBox(aiContent);
	m_aiProviderCombo->addItem(tr("Ollama (local)"), "ollama");
	m_aiProviderCombo->addItem(tr("OpenAI (cloud)"), "openai");
	aiLayout->addWidget(makeSettingsRow(
		tr("Provider"),
		tr("Run models locally with Ollama or connect to OpenAI."),
		m_aiProviderCombo, aiContent));

	m_openaiApiKeyEdit = new QLineEdit(aiContent);
	m_openaiApiKeyEdit->setEchoMode(QLineEdit::Password);
	m_openaiApiKeyEdit->setPlaceholderText(tr("OPENAI_API_KEY takes precedence"));
	aiLayout->addWidget(makeSettingsRow(
		tr("OpenAI API key"),
		tr("Prefer the OPENAI_API_KEY environment variable. Values entered here are stored as plain text."),
		m_openaiApiKeyEdit, aiContent));

	m_openaiModelEdit = new QLineEdit(aiContent);
	m_openaiModelEdit->setPlaceholderText("gpt-4.1-mini");
	aiLayout->addWidget(makeSettingsRow(
		tr("OpenAI model"), tr("Model used for cloud requests."),
		m_openaiModelEdit, aiContent));

	m_openaiBaseUrlEdit = new QLineEdit(aiContent);
	m_openaiBaseUrlEdit->setPlaceholderText("https://api.openai.com/v1");
	aiLayout->addWidget(makeSettingsRow(
		tr("OpenAI base URL"), tr("API endpoint for OpenAI-compatible services."),
		m_openaiBaseUrlEdit, aiContent));

	aiLayout->addWidget(makeSectionTitle(tr("Local model"), aiContent));

	m_ollamaModelEdit = new QLineEdit(aiContent);
	m_ollamaModelEdit->setPlaceholderText("llama3.1");
	aiLayout->addWidget(makeSettingsRow(
		tr("Ollama model"), tr("Name of the model installed in Ollama."),
		m_ollamaModelEdit, aiContent));

	m_ollamaBaseUrlEdit = new QLineEdit(aiContent);
	m_ollamaBaseUrlEdit->setPlaceholderText("http://localhost:11434");
	aiLayout->addWidget(makeSettingsRow(
		tr("Ollama URL"), tr("Address of the local Ollama server."),
		m_ollamaBaseUrlEdit, aiContent));

	auto updateAIVisibility = [this] {
		const QString provider = m_aiProviderCombo->currentData().toString();
		const bool isOpenAI = (provider == "openai");
		m_openaiApiKeyEdit->setEnabled(isOpenAI);
		m_openaiModelEdit->setEnabled(isOpenAI);
		m_openaiBaseUrlEdit->setEnabled(isOpenAI);
		m_ollamaModelEdit->setEnabled(!isOpenAI);
		m_ollamaBaseUrlEdit->setEnabled(!isOpenAI);
	};
	connect(m_aiProviderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, [updateAIVisibility](int) { updateAIVisibility(); });

	aiLayout->addStretch(1);
	aiScroll->setWidget(aiContent);
	aiPageLayout->addWidget(aiScroll);

	tabs->addTab(aiPage, tr("AI"));

	// --- Hardware Debugging tab ---
	m_hardwarePage = new HardwareDebugPage(tabs);
	tabs->insertTab(1, m_hardwarePage, tr("Hardware Probes"));
	tabs->setTabText(1, tr("Hardware probes"));

	auto* buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
		Qt::Horizontal,
		this);
	buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primaryButton"));
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
	        this, &SettingsDialog::save);
	m_saveStatusLabel = new QLabel(this);
	m_saveStatusLabel->setStyleSheet("color: #4ec9b0; padding-left: 4px;");

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	auto* header = new QFrame(this);
	header->setObjectName(QStringLiteral("settingsHeader"));
	auto* headerLayout = new QVBoxLayout(header);
	headerLayout->setContentsMargins(38, 28, 38, 18);
	headerLayout->setSpacing(3);
	auto* title = new QLabel(tr("Settings"), header);
	title->setObjectName(QStringLiteral("settingsTitle"));
	auto* subtitle = new QLabel(tr("Configure QDDD to match your debugging workflow."), header);
	subtitle->setObjectName(QStringLiteral("settingsSubtitle"));
	headerLayout->addWidget(title);
	headerLayout->addWidget(subtitle);
	root->addWidget(header);
	root->addWidget(tabs, 1);

	auto* footer = new QFrame(this);
	footer->setObjectName(QStringLiteral("settingsFooter"));
	auto* footerLayout = new QHBoxLayout(footer);
	footerLayout->setContentsMargins(38, 14, 38, 22);
	footerLayout->addWidget(m_saveStatusLabel, 1);
	footerLayout->addWidget(buttons);
	root->addWidget(footer);

	load();
}

void SettingsDialog::load()
{
	QSettings s;

	// Hardware debug page
	if (m_hardwarePage)
		m_hardwarePage->load();

	const QString backend = s.value(kKeyBackend, "gdb-mi").toString();
	const int backendIdx = m_backendCombo->findData(backend);
	m_backendCombo->setCurrentIndex(backendIdx >= 0 ? backendIdx : 0);

	m_gdbPathEdit->setText(s.value(kKeyGdbPath, "gdb").toString());
	m_lldbMiPathEdit->setText(s.value(kKeyLldbMiPath, "/usr/local/bin/lldb-mi").toString());
	const QString reverseMode = s.value(kKeyReverseMode, "auto").toString();
	const int reverseIdx = m_reverseModeCombo->findData(reverseMode);
	m_reverseModeCombo->setCurrentIndex(reverseIdx >= 0 ? reverseIdx : 0);

	const QString tgt = s.value(kKeyTargetType, "local").toString();
	const int tgtIdx = m_targetTypeCombo->findData(tgt);
	m_targetTypeCombo->setCurrentIndex(tgtIdx >= 0 ? tgtIdx : 0);
	m_remoteHostEdit->setText(s.value(kKeyRemoteHost, "127.0.0.1").toString());
	m_remotePortSpin->setValue(s.value(kKeyRemotePort, 3333).toInt());

	m_openaiApiKeyEdit->setText(s.value(kKeyOpenAIKey).toString());
	m_openaiModelEdit->setText(s.value(kKeyOpenAIModel, "gpt-4.1-mini").toString());
	m_openaiBaseUrlEdit->setText(s.value(kKeyOpenAIBaseUrl, "https://api.openai.com/v1").toString());

	m_aiEnabledCheck->setChecked(s.value(kKeyAIEnabled, true).toBool());

	const QString provider = s.value(kKeyAIProvider, "ollama").toString();
	const int providerIdx = m_aiProviderCombo->findData(provider);
	m_aiProviderCombo->setCurrentIndex(providerIdx >= 0 ? providerIdx : 0);
	m_ollamaModelEdit->setText(s.value(kKeyOllamaModel, "llama3.1").toString());
	m_ollamaBaseUrlEdit->setText(s.value(kKeyOllamaBaseUrl, "http://localhost:11434").toString());

	{
		const QString curProvider = m_aiProviderCombo->currentData().toString();
		const bool isOpenAI = (curProvider == "openai");
		m_openaiApiKeyEdit->setEnabled(isOpenAI);
		m_openaiModelEdit->setEnabled(isOpenAI);
		m_openaiBaseUrlEdit->setEnabled(isOpenAI);
		m_ollamaModelEdit->setEnabled(!isOpenAI);
		m_ollamaBaseUrlEdit->setEnabled(!isOpenAI);
	}
}

void SettingsDialog::save()
{
	QSettings s;

	// Save hardware debug page (flushes UI to config manager, then persists)
	if (m_hardwarePage)
		m_hardwarePage->save();

	s.setValue(kKeyBackend, m_backendCombo->currentData().toString());
	s.setValue(kKeyGdbPath, m_gdbPathEdit->text().trimmed());
	s.setValue(kKeyLldbMiPath, m_lldbMiPathEdit->text().trimmed());
	s.setValue(kKeyReverseMode, m_reverseModeCombo->currentData().toString());

	s.setValue(kKeyTargetType, m_targetTypeCombo->currentData().toString());
	s.setValue(kKeyRemoteHost, m_remoteHostEdit->text().trimmed());
	s.setValue(kKeyRemotePort, m_remotePortSpin->value());

	s.setValue(kKeyOpenAIKey, m_openaiApiKeyEdit->text().trimmed());
	s.setValue(kKeyOpenAIModel, m_openaiModelEdit->text().trimmed());
	s.setValue(kKeyOpenAIBaseUrl, m_openaiBaseUrlEdit->text().trimmed());

	s.setValue(kKeyAIEnabled, m_aiEnabledCheck->isChecked());
	s.setValue(kKeyAIProvider, m_aiProviderCombo->currentData().toString());
	s.setValue(kKeyOllamaModel, m_ollamaModelEdit->text().trimmed());
	s.setValue(kKeyOllamaBaseUrl, m_ollamaBaseUrlEdit->text().trimmed());
	s.sync();
	if (m_saveStatusLabel)
		m_saveStatusLabel->setText(tr("Settings saved."));
}

void SettingsDialog::browseGdb()
{
	const QString path = withBrowse(m_gdbPathEdit, tr("Select GDB executable"), this);
	if (!path.isEmpty())
		m_gdbPathEdit->setText(path);
}

void SettingsDialog::browseLldbMi()
{
	const QString path = withBrowse(m_lldbMiPathEdit, tr("Select LLDB-MI executable"), this);
	if (!path.isEmpty())
		m_lldbMiPathEdit->setText(path);
}
