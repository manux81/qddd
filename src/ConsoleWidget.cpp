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

#include "ConsoleWidget.h"
#include "DebugSession.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextOption>
#include <QVBoxLayout>

ConsoleWidget::ConsoleWidget(QWidget *parent)
    : QWidget(parent), m_output(new QPlainTextEdit(this)),
      m_input(new QLineEdit(this)) {
	auto *layout = new QVBoxLayout(this);
	m_output->setReadOnly(true);
	m_output->setWordWrapMode(QTextOption::NoWrap);

	m_input->setPlaceholderText(tr("Comando GDB / MI..."));

	layout->addWidget(m_output);
	layout->addWidget(m_input);
	setLayout(layout);

	connect(m_input, &QLineEdit::returnPressed, this,
	        &ConsoleWidget::onCommandEntered);
}

void ConsoleWidget::setSession(DebugSession *session) {
	if (m_session) {
		disconnect(m_session, nullptr, this, nullptr);
	}
	m_session = session;
	if (m_session) {
		connect(m_session, &DebugSession::outputReceived, this,
		        &ConsoleWidget::appendOutput);
	}
}

void ConsoleWidget::appendOutput(const QString &text) {
	if (text.isEmpty())
		return;
	m_output->appendPlainText(text);
	QScrollBar *bar = m_output->verticalScrollBar();
	bar->setValue(bar->maximum());
}

void ConsoleWidget::onCommandEntered() {
	const QString cmd = m_input->text();
	if (cmd.isEmpty())
		return;

	if (m_session) {
		m_output->appendPlainText(QStringLiteral("> ") + cmd);
		m_session->sendCommand(cmd);
	}
	m_input->clear();
}
