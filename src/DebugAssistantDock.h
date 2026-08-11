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

#include <QWidget>

class DebuggerSession;
class QPushButton;
class QLineEdit;
class QTextBrowser;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QTreeWidget;
class QToolButton;

class DebugAssistantDock final : public QWidget
{
	Q_OBJECT

public:
	explicit DebugAssistantDock(DebuggerSession* session, QWidget* parent = nullptr);
	~DebugAssistantDock() override = default;

	void setLastStopLocation(const QString& file, int line, const QString& function);

private slots:
	void suggest();
	void sendUserMessage();
	void applySelectedActions();
	void onReplyFinished(QNetworkReply* reply);

private:
	QByteArray buildRequestBody(const QString& userText) const;
	QString extractResponseText(const QByteArray& json) const;
	void populateActionsFromJsonText(const QString& text);
	void appendUser(const QString& text);
	void appendAssistantText(const QString& text);
	void appendInfo(const QString& text);
	void appendError(const QString& text);
	void setBusy(bool busy);
	void updateApplyEnabled();

	DebuggerSession* m_session = nullptr;
	QString m_lastUserText;
	QString m_lastFile;
	QString m_lastFunction;
	int m_lastLine = 0;

	QLabel* m_contextLabel = nullptr;
	QLabel* m_providerLabel = nullptr;
	QToolButton* m_clearBtn = nullptr;

	QTextBrowser* m_log = nullptr;
	QLineEdit* m_input = nullptr;
	QPushButton* m_sendBtn = nullptr;
	QPushButton* m_suggestBtn = nullptr;

	QTreeWidget* m_actions = nullptr;
	QPushButton* m_applyBtn = nullptr;

	QNetworkAccessManager* m_net = nullptr;
};
