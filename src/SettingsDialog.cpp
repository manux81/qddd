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

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
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

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Settings"));
	setModal(true);
	resize(900, 680);
	setMinimumSize(760, 560);

	auto* tabs = new QTabWidget(this);

	// --- Debugger tab ---
	auto* debuggerPage = new QWidget(tabs);
	auto* dbgLayout = new QVBoxLayout(debuggerPage);
	auto* debuggerIntro = new QLabel(
		tr("Choose the debugger engine here. The target itself is selected from the main window."),
		debuggerPage);
	debuggerIntro->setWordWrap(true);
	debuggerIntro->setStyleSheet("padding: 8px; background: rgba(70,120,180,35); border-radius: 5px;");
	dbgLayout->addWidget(debuggerIntro);

	auto* dbgBox = new QGroupBox(tr("Debugger Backend"), debuggerPage);
	auto* dbgForm = new QFormLayout(dbgBox);

	m_backendCombo = new QComboBox(dbgBox);
	m_backendCombo->addItem(tr("GDB (MI2)"), "gdb-mi");
	m_backendCombo->addItem(tr("LLDB (lldb-mi / MI3)"), "lldb-mi");
	dbgForm->addRow(tr("Backend:"), m_backendCombo);

	auto* gdbRow = new QWidget(dbgBox);
	auto* gdbRowLayout = new QHBoxLayout(gdbRow);
	gdbRowLayout->setContentsMargins(0, 0, 0, 0);
	m_gdbPathEdit = new QLineEdit(gdbRow);
	auto* gdbBrowse = new QPushButton(tr("Browse..."), gdbRow);
	connect(gdbBrowse, &QPushButton::clicked, this, &SettingsDialog::browseGdb);
	gdbRowLayout->addWidget(m_gdbPathEdit, 1);
	gdbRowLayout->addWidget(gdbBrowse);
	dbgForm->addRow(tr("GDB path:"), gdbRow);

	auto* lldbRow = new QWidget(dbgBox);
	auto* lldbRowLayout = new QHBoxLayout(lldbRow);
	lldbRowLayout->setContentsMargins(0, 0, 0, 0);
	m_lldbMiPathEdit = new QLineEdit(lldbRow);
	auto* lldbBrowse = new QPushButton(tr("Browse..."), lldbRow);
	connect(lldbBrowse, &QPushButton::clicked, this, &SettingsDialog::browseLldbMi);
	lldbRowLayout->addWidget(m_lldbMiPathEdit, 1);
	lldbRowLayout->addWidget(lldbBrowse);
	dbgForm->addRow(tr("LLDB-MI path:"), lldbRow);

	m_reverseModeCombo = new QComboBox(dbgBox);
	m_reverseModeCombo->addItem(tr("Auto (safe branch tracing)"), "auto");
	m_reverseModeCombo->addItem(tr("Branch trace (recommended)"), "btrace");
	m_reverseModeCombo->addItem(tr("Full record (experimental)"), "full");
	m_reverseModeCombo->addItem(tr("Disabled"), "disabled");
	dbgForm->addRow(tr("Reverse execution:"), m_reverseModeCombo);

	auto* reverseHint = new QLabel(
		tr("Auto and Branch trace never fall back to software recording. Full record is experimental and can fail in system libraries or on unsupported syscalls."),
		dbgBox);
	reverseHint->setWordWrap(true);
	reverseHint->setStyleSheet("color: #d8a657;");
	dbgForm->addRow(QString(), reverseHint);

	auto* hint = new QLabel(
		tr("Note: changing backend/path applies the next time a debugging session is started."),
		debuggerPage);
	hint->setWordWrap(true);
	hint->setStyleSheet("color: rgba(255,255,255,140);");

	dbgLayout->addWidget(dbgBox);
	dbgLayout->addWidget(hint);

	// --- Target tab (classic remote gdbserver / local) ---
	auto* targetPage = new QWidget(tabs);
	auto* targetLayout = new QVBoxLayout(targetPage);

	auto* targetBox = new QGroupBox(tr("External GDB server"), targetPage);
	auto* targetForm = new QFormLayout(targetBox);

	m_targetTypeCombo = new QComboBox(targetBox);
	m_targetTypeCombo->addItem(tr("Local process"), "local");
	m_targetTypeCombo->addItem(tr("External GDB server"), "gdbserver");
	m_targetTypeCombo->hide(); // target mode is selected explicitly in the main window

	m_remoteHostEdit = new QLineEdit(targetBox);
	m_remoteHostEdit->setPlaceholderText("127.0.0.1");
	targetForm->addRow(tr("Remote host:"), m_remoteHostEdit);

	m_remotePortSpin = new QSpinBox(targetBox);
	m_remotePortSpin->setRange(1, 65535);
	m_remotePortSpin->setValue(3333);
	targetForm->addRow(tr("Remote port:"), m_remotePortSpin);

	auto* targetHint = new QLabel(
		tr("Use this only when a GDB server is already running outside QDDD. For ST-LINK, J-Link or OpenOCD managed by QDDD, create a Hardware Probe profile."),
		targetPage);
	targetHint->setWordWrap(true);
	targetHint->setStyleSheet("color: rgba(255,255,255,140);");

	dbgLayout->addWidget(targetBox);
	dbgLayout->addWidget(targetHint);
	dbgLayout->addStretch(1);

	tabs->addTab(debuggerPage, tr("Debugger"));

	// --- AI tab ---
	auto* aiPage = new QWidget(tabs);
	auto* aiLayout = new QVBoxLayout(aiPage);

	auto* aiBox = new QGroupBox(tr("LLM Provider"), aiPage);
	auto* aiForm = new QFormLayout(aiBox);

	m_aiEnabledCheck = new QCheckBox(tr("Enable Debug Assistant"), aiBox);
	aiForm->addRow(QString(), m_aiEnabledCheck);

	m_aiProviderCombo = new QComboBox(aiBox);
	m_aiProviderCombo->addItem(tr("Ollama (local)"), "ollama");
	m_aiProviderCombo->addItem(tr("OpenAI (cloud)"), "openai");
	aiForm->addRow(tr("Provider:"), m_aiProviderCombo);

	m_openaiApiKeyEdit = new QLineEdit(aiBox);
	m_openaiApiKeyEdit->setEchoMode(QLineEdit::Password);
	m_openaiApiKeyEdit->setPlaceholderText(tr("OPENAI_API_KEY takes precedence"));
	aiForm->addRow(tr("API key:"), m_openaiApiKeyEdit);
	auto* openaiKeyWarning = new QLabel(
		tr("Prefer the OPENAI_API_KEY environment variable. The fallback API-key field is stored by QSettings in plain text and is not a secure credential store."),
		aiBox);
	openaiKeyWarning->setWordWrap(true);
	openaiKeyWarning->setStyleSheet("color: #d8a657;");
	aiForm->addRow(QString(), openaiKeyWarning);

	m_openaiModelEdit = new QLineEdit(aiBox);
	m_openaiModelEdit->setPlaceholderText("gpt-4.1-mini");
	aiForm->addRow(tr("Model:"), m_openaiModelEdit);

	m_openaiBaseUrlEdit = new QLineEdit(aiBox);
	m_openaiBaseUrlEdit->setPlaceholderText("https://api.openai.com/v1");
	aiForm->addRow(tr("Base URL:"), m_openaiBaseUrlEdit);

	m_ollamaModelEdit = new QLineEdit(aiBox);
	m_ollamaModelEdit->setPlaceholderText("llama3.1");
	aiForm->addRow(tr("Ollama model:"), m_ollamaModelEdit);

	m_ollamaBaseUrlEdit = new QLineEdit(aiBox);
	m_ollamaBaseUrlEdit->setPlaceholderText("http://localhost:11434");
	aiForm->addRow(tr("Ollama URL:"), m_ollamaBaseUrlEdit);

	auto* aiHint = new QLabel(
		tr("The Debug Assistant sends a compact snapshot of your debug session to the model to propose explanations and actions."),
		aiPage);
	aiHint->setWordWrap(true);
	aiHint->setStyleSheet("color: rgba(255,255,255,140);");

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

	aiLayout->addWidget(aiBox);
	aiLayout->addWidget(aiHint);
	aiLayout->addStretch(1);

	tabs->addTab(aiPage, tr("AI"));

	// --- Hardware Debugging tab ---
	m_hardwarePage = new HardwareDebugPage(tabs);
	tabs->insertTab(1, m_hardwarePage, tr("Hardware Probes"));

	auto* buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
		Qt::Horizontal,
		this);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
	        this, &SettingsDialog::save);
	m_saveStatusLabel = new QLabel(this);
	m_saveStatusLabel->setStyleSheet("color: #7cc47c; padding-left: 4px;");

	auto* root = new QVBoxLayout(this);
	root->addWidget(tabs, 1);
	root->addWidget(m_saveStatusLabel);
	root->addWidget(buttons);

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
