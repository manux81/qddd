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
#include "BreakpointEditorDialog.h"
#include "DebugSession.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QRegularExpressionValidator>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QScreen>

BreakpointEditorDialog::BreakpointEditorDialog(DebuggerSession* session,
											   int breakpointId,
											   QWidget* parent)
	: QDialog(parent)
	, m_session(session)
	, m_breakpointId(breakpointId)
{
	setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
	setAttribute(Qt::WA_DeleteOnClose);
	setModal(false);
	setWindowTitle(tr("Edit Breakpoint"));

	buildUi();
	loadFromBreakpoint();
	connectSignals();
}

void BreakpointEditorDialog::buildUi()
{
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(8);

	// Enable checkbox (title)
	m_enableCheck = new QCheckBox(tr("Enable Breakpoint"), this);
	root->addWidget(m_enableCheck);

	// ---- Form ----
	auto* form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignLeft);
	form->setFormAlignment(Qt::AlignTop);

	// Name
	m_nameEdit = new QLineEdit(this);
	m_nameEdit->setPlaceholderText(tr("Breakpoint name"));

	QRegularExpression rx("^[A-Za-z_][A-Za-z0-9_]*$");
	m_nameEdit->setValidator(new QRegularExpressionValidator(rx, this));

	form->addRow(tr("Name"), m_nameEdit);

	auto* nameHint = new QLabel(
		tr("A breakpoint name cannot start with numbers or contain whitespace."),
		this);
	nameHint->setStyleSheet("color: gray; font-size: 11px;");
	form->addRow(QString(), nameHint);

	// Condition
	m_conditionEdit = new QLineEdit(this);
	form->addRow(tr("Condition"), m_conditionEdit);

	// Temporary
	m_temporaryCheck = new QCheckBox(tr("Temporary (one-shot)"), this);
	form->addRow(tr("Type"), m_temporaryCheck);

	// Ignore
	m_ignoreSpin = new QSpinBox(this);
	m_ignoreSpin->setRange(0, 1000000);

	auto* ignoreLayout = new QHBoxLayout;
	ignoreLayout->addWidget(m_ignoreSpin);
	ignoreLayout->addWidget(new QLabel(tr("times before stopping"), this));
	ignoreLayout->addStretch();

	form->addRow(tr("Ignore"), ignoreLayout);

	// ---- Actions ----
	auto* actionRow = new QHBoxLayout;

	m_actionTypeCombo = new QComboBox(this);
	m_actionTypeCombo->addItem(tr("Debugger Command"));
	m_actionTypeCombo->addItem(tr("Log Message"));
	m_actionTypeCombo->addItem(tr("Shell Command"));
	m_actionTypeCombo->addItem(tr("Sound"));

	actionRow->addWidget(m_actionTypeCombo);

	auto* addBtn = new QPushButton("+", this);
	auto* remBtn = new QPushButton("−", this);
	addBtn->setFixedWidth(28);
	remBtn->setFixedWidth(28);

	actionRow->addWidget(addBtn);
	actionRow->addWidget(remBtn);

	form->addRow(tr("Action"), actionRow);

	m_actionPayloadEdit = new QLineEdit(this);
	form->addRow(QString(), m_actionPayloadEdit);

	m_actionList = new QListWidget(this);
	form->addRow(QString(), m_actionList);

	// ---- Options ----
	m_autoContinueCheck = new QCheckBox(
		tr("Automatically continue after evaluating actions"), this);
	form->addRow(tr("Options"), m_autoContinueCheck);

	root->addLayout(form);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
	root->addWidget(buttons);

	connect(addBtn, &QPushButton::clicked, this, &BreakpointEditorDialog::addAction);
	connect(remBtn, &QPushButton::clicked, this, &BreakpointEditorDialog::removeSelectedAction);
}

void BreakpointEditorDialog::loadFromBreakpoint()
{
	const auto& bps = m_session->breakpoints();
	for (const auto& bp : bps) {
		if (bp.number == m_breakpointId) {
			m_enableCheck->setChecked(bp.enabled);
			m_conditionEdit->setText(bp.condition);
			m_ignoreSpin->setValue(bp.ignoreCount);
			m_temporaryCheck->setChecked(bp.temporary);
			m_autoContinueCheck->setChecked(bp.autoContinue);

			for (const auto& action : bp.actions) {
				m_actionList->addItem(action.payload);
			}
			break;
		}
	}
}

void BreakpointEditorDialog::connectSignals()
{
	connect(m_enableCheck, &QCheckBox::toggled,
			this, &BreakpointEditorDialog::applyEnabled);

	connect(m_conditionEdit, &QLineEdit::editingFinished,
			this, &BreakpointEditorDialog::applyCondition);

	connect(m_temporaryCheck, &QCheckBox::toggled,
	        this, &BreakpointEditorDialog::applyTemporary);

	connect(m_ignoreSpin, qOverload<int>(&QSpinBox::valueChanged),
			this, &BreakpointEditorDialog::applyIgnoreCount);

	connect(m_autoContinueCheck, &QCheckBox::toggled,
			this, &BreakpointEditorDialog::applyAutoContinue);
}

void BreakpointEditorDialog::applyEnabled()
{
	m_session->setBreakpointEnabled(m_breakpointId, m_enableCheck->isChecked());
}

void BreakpointEditorDialog::applyCondition()
{
	m_session->updateBreakpointCondition(m_breakpointId, m_conditionEdit->text());
}

void BreakpointEditorDialog::applyIgnoreCount()
{
	m_session->updateBreakpointIgnoreCount(m_breakpointId, m_ignoreSpin->value());
}

void BreakpointEditorDialog::applyAutoContinue()
{
	// Not wired to backend yet.
}

void BreakpointEditorDialog::applyTemporary()
{
	m_session->updateBreakpointTemporary(m_breakpointId, m_temporaryCheck->isChecked());
}

void BreakpointEditorDialog::addAction()
{
	if (m_actionPayloadEdit->text().isEmpty())
		return;

	m_actionList->addItem(
		m_actionTypeCombo->currentText() + ": " +
		m_actionPayloadEdit->text());

	applyActions();
	m_actionPayloadEdit->clear();
}

void BreakpointEditorDialog::removeSelectedAction()
{
	delete m_actionList->takeItem(m_actionList->currentRow());
	applyActions();
}

void BreakpointEditorDialog::applyActions()
{
	// Qui richiami la tua API:
	// m_session->setBreakpointActions(...)
}
