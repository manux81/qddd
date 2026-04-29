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

#include "SettingsDialog.h"

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
constexpr const char* kKeyTargetType   = "target/type";
constexpr const char* kKeyRemoteHost   = "target/remoteHost";
constexpr const char* kKeyRemotePort   = "target/remotePort";

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
	resize(620, 360);

	auto* tabs = new QTabWidget(this);

	// --- Debugger tab ---
	auto* debuggerPage = new QWidget(tabs);
	auto* dbgLayout = new QVBoxLayout(debuggerPage);

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

	auto* hint = new QLabel(
		tr("Note: changing backend/path applies the next time a debugging session is started."),
		debuggerPage);
	hint->setWordWrap(true);
	hint->setStyleSheet("color: rgba(255,255,255,140);");

	dbgLayout->addWidget(dbgBox);
	dbgLayout->addWidget(hint);
	dbgLayout->addStretch(1);

	// --- Target tab (future-proofing) ---
	auto* targetPage = new QWidget(tabs);
	auto* targetLayout = new QVBoxLayout(targetPage);

	auto* targetBox = new QGroupBox(tr("Target Connection"), targetPage);
	auto* targetForm = new QFormLayout(targetBox);

	m_targetTypeCombo = new QComboBox(targetBox);
	m_targetTypeCombo->addItem(tr("Local executable"), "local");
	m_targetTypeCombo->addItem(tr("Remote gdbserver"), "gdbserver");
	m_targetTypeCombo->addItem(tr("ST-Link (future)"), "stlink");
	m_targetTypeCombo->addItem(tr("J-Link (future)"), "jlink");
	m_targetTypeCombo->addItem(tr("Lauterbach (future)"), "lauterbach");
	targetForm->addRow(tr("Type:"), m_targetTypeCombo);

	m_remoteHostEdit = new QLineEdit(targetBox);
	m_remoteHostEdit->setPlaceholderText("127.0.0.1");
	targetForm->addRow(tr("Remote host:"), m_remoteHostEdit);

	m_remotePortSpin = new QSpinBox(targetBox);
	m_remotePortSpin->setRange(1, 65535);
	m_remotePortSpin->setValue(3333);
	targetForm->addRow(tr("Remote port:"), m_remotePortSpin);

	auto* targetHint = new QLabel(
		tr("Remote/ST-Link/J-Link/Lauterbach integration will be wired in later; settings are stored now."),
		targetPage);
	targetHint->setWordWrap(true);
	targetHint->setStyleSheet("color: rgba(255,255,255,140);");

	targetLayout->addWidget(targetBox);
	targetLayout->addWidget(targetHint);
	targetLayout->addStretch(1);

	tabs->addTab(debuggerPage, tr("Debugger"));
	tabs->addTab(targetPage, tr("Target"));

	auto* buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
		Qt::Horizontal,
		this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto* root = new QVBoxLayout(this);
	root->addWidget(tabs, 1);
	root->addWidget(buttons);

	load();
}

void SettingsDialog::load()
{
	QSettings s;

	const QString backend = s.value(kKeyBackend, "gdb-mi").toString();
	const int backendIdx = m_backendCombo->findData(backend);
	m_backendCombo->setCurrentIndex(backendIdx >= 0 ? backendIdx : 0);

	m_gdbPathEdit->setText(s.value(kKeyGdbPath, "gdb").toString());
	m_lldbMiPathEdit->setText(s.value(kKeyLldbMiPath, "/usr/local/bin/lldb-mi").toString());

	const QString tgt = s.value(kKeyTargetType, "local").toString();
	const int tgtIdx = m_targetTypeCombo->findData(tgt);
	m_targetTypeCombo->setCurrentIndex(tgtIdx >= 0 ? tgtIdx : 0);
	m_remoteHostEdit->setText(s.value(kKeyRemoteHost, "127.0.0.1").toString());
	m_remotePortSpin->setValue(s.value(kKeyRemotePort, 3333).toInt());
}

void SettingsDialog::save() const
{
	QSettings s;
	s.setValue(kKeyBackend, m_backendCombo->currentData().toString());
	s.setValue(kKeyGdbPath, m_gdbPathEdit->text().trimmed());
	s.setValue(kKeyLldbMiPath, m_lldbMiPathEdit->text().trimmed());

	s.setValue(kKeyTargetType, m_targetTypeCombo->currentData().toString());
	s.setValue(kKeyRemoteHost, m_remoteHostEdit->text().trimmed());
	s.setValue(kKeyRemotePort, m_remotePortSpin->value());
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

