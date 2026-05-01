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

#include "DebugAssistantDock.h"

#include "DebugSession.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QJsonArray topFrames(const QVector<StackFrame>& frames, int limit)
{
	QJsonArray arr;
	for (int i = 0; i < frames.size() && i < limit; ++i) {
		const auto& f = frames[i];
		QJsonObject o;
		o["level"] = f.level;
		o["function"] = f.function;
		o["file"] = f.file;
		o["line"] = f.line;
		arr.push_back(o);
	}
	return arr;
}

QJsonArray topVariables(const std::vector<std::unique_ptr<DebugVariable>>& vars, int limit)
{
	QJsonArray arr;
	int n = 0;
	for (const auto& v : vars) {
		if (!v)
			continue;
		QJsonObject o;
		o["name"] = v->name;
		o["value"] = v->value;
		o["type"] = v->type;
		o["address"] = v->address;
		arr.push_back(o);
		if (++n >= limit)
			break;
	}
	return arr;
}

QJsonArray topBreakpoints(const QVector<BreakpointInfo>& bps, int limit)
{
	QJsonArray arr;
	for (int i = 0; i < bps.size() && i < limit; ++i) {
		const auto& bp = bps[i];
		QJsonObject o;
		o["id"] = bp.number;
		o["file"] = bp.file;
		o["line"] = bp.line;
		o["enabled"] = bp.enabled;
		o["pending"] = bp.pending;
		o["temporary"] = bp.temporary;
		o["originalLocation"] = bp.originalLocation;
		arr.push_back(o);
	}
	return arr;
}

QJsonArray topChanged(const QSet<QString>& changed, int limit)
{
	QJsonArray arr;
	int n = 0;
	for (const auto& p : changed) {
		arr.push_back(p);
		if (++n >= limit)
			break;
	}
	return arr;
}

QJsonObject responseSchema()
{
	// Keep it small and robust: suggestions + executable actions.
	QJsonObject actionProps;
	actionProps["type"] = QJsonObject{{"type", "string"}};
	actionProps["rationale"] = QJsonObject{{"type", "string"}};
	// With strict schema adherence, objects must specify additionalProperties=false.
	// To keep args flexible, represent them as an array of {key,value} pairs.
	actionProps["args"] = QJsonObject{
		{"type", "array"},
		{"items", QJsonObject{
			{"type", "object"},
			{"additionalProperties", false},
			{"properties", QJsonObject{
				{"key", QJsonObject{{"type", "string"}}},
				{"value", QJsonObject{{"type", "string"}}}
			}},
			{"required", QJsonArray{"key", "value"}}
		}}
	};

	QJsonObject rootProps;
	rootProps["summary"] = QJsonObject{{"type", "string"}};
	rootProps["hypotheses"] = QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}};
	rootProps["evidence"] = QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}};
	rootProps["actions"] = QJsonObject{
		{"type", "array"},
		{"items", QJsonObject{{"type", "object"},
		                      {"additionalProperties", false},
		                      {"properties", actionProps},
		                      {"required", QJsonArray{"type", "rationale", "args"}}}}
	};

	return QJsonObject{
		{"type", "object"},
		{"additionalProperties", false},
		{"properties", rootProps},
		{"required", QJsonArray{"summary", "hypotheses", "evidence", "actions"}}
	};
}

} // namespace

DebugAssistantDock::DebugAssistantDock(DebuggerSession* session, QWidget* parent)
	: QWidget(parent), m_session(session)
{
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(8);

	m_contextLabel = new QLabel(tr("No stop location yet."), this);
	m_contextLabel->setStyleSheet("color: rgba(255,255,255,170);");
	m_contextLabel->setWordWrap(true);
	root->addWidget(m_contextLabel);

	// Header row: provider status + clear
	auto* headerRow = new QHBoxLayout();
	m_providerLabel = new QLabel(this);
	m_providerLabel->setStyleSheet("color: rgba(255,255,255,140);");
	headerRow->addWidget(m_providerLabel, 1);
	m_clearBtn = new QToolButton(this);
	m_clearBtn->setText(tr("Clear"));
	headerRow->addWidget(m_clearBtn);
	connect(m_clearBtn, &QToolButton::clicked, this, [this] {
		if (m_log)
			m_log->clear();
	});
	root->addLayout(headerRow);

	auto* split = new QSplitter(Qt::Vertical, this);
	split->setChildrenCollapsible(false);

	m_log = new QTextBrowser(split);
	m_log->setOpenExternalLinks(false);
	m_log->setReadOnly(true);
	m_log->setPlaceholderText(tr("Assistant output will appear here."));
	m_log->setStyleSheet("QTextBrowser { background: #1e1e1e; border: 1px solid #3c3c3c; }");
	split->addWidget(m_log);

	auto* inputRow = new QHBoxLayout();
	m_input = new QLineEdit(this);
	m_input->setPlaceholderText(tr("Ask about the current debug state, e.g. \"Why is ptr null?\""));
	m_sendBtn = new QPushButton(tr("Send"), this);
	m_suggestBtn = new QPushButton(tr("Suggest Next"), this);
	inputRow->addWidget(m_input, 1);
	inputRow->addWidget(m_sendBtn);
	inputRow->addWidget(m_suggestBtn);
	root->addLayout(inputRow);

	connect(m_sendBtn, &QPushButton::clicked, this, &DebugAssistantDock::sendUserMessage);
	connect(m_suggestBtn, &QPushButton::clicked, this, &DebugAssistantDock::suggest);
	connect(m_input, &QLineEdit::returnPressed, this, &DebugAssistantDock::sendUserMessage);

	auto* actionsPane = new QWidget(split);
	auto* actionsLayout = new QVBoxLayout(actionsPane);
	actionsLayout->setContentsMargins(0, 0, 0, 0);
	actionsLayout->setSpacing(6);

	auto* actionsTitle = new QLabel(tr("Proposed Actions"), actionsPane);
	actionsTitle->setStyleSheet("color: rgba(255,255,255,170); font-weight: 600;");
	actionsLayout->addWidget(actionsTitle);

	m_actions = new QTreeWidget(actionsPane);
	m_actions->setColumnCount(3);
	m_actions->setHeaderLabels({tr("Action"), tr("Args"), tr("Rationale")});
	m_actions->setRootIsDecorated(false);
	m_actions->setAlternatingRowColors(true);
	m_actions->setSelectionMode(QAbstractItemView::NoSelection);
	m_actions->setStyleSheet("QTreeWidget { background: #1e1e1e; border: 1px solid #3c3c3c; }");
	actionsLayout->addWidget(m_actions, 1);

	m_applyBtn = new QPushButton(tr("Apply Selected"), actionsPane);
	m_applyBtn->setEnabled(false);
	connect(m_applyBtn, &QPushButton::clicked, this, &DebugAssistantDock::applySelectedActions);
	actionsLayout->addWidget(m_applyBtn);

	connect(m_actions, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem*, int) {
		updateApplyEnabled();
	});

	split->addWidget(actionsPane);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 2);

	root->addWidget(split, 1);

	m_net = new QNetworkAccessManager(this);
	connect(m_net, &QNetworkAccessManager::finished, this, &DebugAssistantDock::onReplyFinished);

	updateApplyEnabled();
	setBusy(false);
	appendInfo(tr("Configure provider/model in Settings → AI (Ollama recommended for local)."));
}

void DebugAssistantDock::setLastStopLocation(const QString& file, int line, const QString& function)
{
	m_lastFile = file;
	m_lastLine = line;
	m_lastFunction = function;

	const QString shown = file.isEmpty()
		? tr("Stopped (unknown location)")
		: tr("Stopped at %1:%2 (%3)").arg(file).arg(line).arg(function);
	m_contextLabel->setText(shown);
}

void DebugAssistantDock::suggest()
{
	m_input->setText(tr("Suggest next debugging steps and propose actions."));
	sendUserMessage();
}

void DebugAssistantDock::sendUserMessage()
{
	const QString userText = m_input->text().trimmed();
	if (userText.isEmpty())
		return;

	m_input->clear();
	appendUser(userText);
	setBusy(true);

	QSettings s;
	const QString provider = s.value("ai/provider", "ollama").toString().trimmed();
	if (provider == "openai") {
		const QString apiKey = s.value("ai/openaiApiKey").toString().trimmed();
		const QString baseUrl = s.value("ai/openaiBaseUrl", "https://api.openai.com/v1").toString().trimmed();
		if (apiKey.isEmpty()) {
			appendError("Missing API key (Settings → AI).");
			setBusy(false);
			return;
		}

		QUrl url(baseUrl + "/responses");
		QNetworkRequest req(url);
		req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		req.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());

		const QByteArray body = buildRequestBody(userText);
		appendInfo(QString("Request(OpenAI): %1 bytes, contains \"\\\"kv\\\"\"=%2")
			          .arg(body.size())
			          .arg(body.contains("\"kv\"") ? "yes" : "no"));
		m_net->post(req, body);
		return;
	}

	// Ollama (local)
	const QString baseUrl = s.value("ai/ollamaBaseUrl", "http://localhost:11434").toString().trimmed();
	QUrl url(baseUrl + "/api/chat");
	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

	const QByteArray body = buildRequestBody(userText); // reused: switches by provider in buildRequestBody
	appendInfo(QString("Request(Ollama): %1 bytes").arg(body.size()));
	m_net->post(req, body);
}

QByteArray DebugAssistantDock::buildRequestBody(const QString& userText) const
{
	QSettings s;
	const QString provider = s.value("ai/provider", "ollama").toString().trimmed();

	QJsonObject ctx;
	ctx["stop"] = QJsonObject{
		{"file", m_lastFile},
		{"line", m_lastLine},
		{"function", m_lastFunction},
	};

	if (m_session) {
		ctx["stack"] = topFrames(m_session->stackFrames(), 12);
		ctx["variables"] = topVariables(m_session->variables(), 40);
		ctx["breakpoints"] = topBreakpoints(m_session->breakpoints(), 50);
		ctx["changedPaths"] = topChanged(m_session->changedPaths(), 60);
		ctx["reverseSupported"] = m_session->supportsReverseExecution();
	}

	const QString systemPrompt =
		"You are a debugging assistant embedded in a GUI debugger.\n"
		"You receive the current debug session state as JSON.\n"
		"Return ONLY valid JSON matching the provided JSON schema.\n"
		"Propose actionable next steps with minimal, safe actions.\n"
		"Prefer actions like evaluate expressions, run-to-cursor, stepping, and setting/toggling breakpoints.\n"
		"Do not hallucinate facts about memory or program state: rely on provided context and ask for an evaluation action when needed.\n"
		"\n"
		"Available action types and args (array of {key,value} strings):\n"
		"- evaluate: key=expr\n"
		"- set_breakpoint / toggle_breakpoint: key=location (\"file:line\" or function name)\n"
		"- run_to_cursor: keys file, line\n"
		"- select_frame: key=index\n"
		"- step_into|step_over|step_out|continue|interrupt: no args\n"
		"- set_variable: keys path, value\n"
		"- raw_command: key=cmd\n";

	const QString userPayload =
		QString("User request:\n%1\n\nDebug context JSON:\n%2")
			.arg(userText)
			.arg(QString::fromUtf8(QJsonDocument(ctx).toJson(QJsonDocument::Compact)));

	auto asInputText = [](const QString& s) {
		return QJsonArray{ QJsonObject{{"type", "input_text"}, {"text", s}} };
	};

	if (provider == "openai") {
		const QString model = s.value("ai/openaiModel", "gpt-4.1-mini").toString().trimmed();
		QJsonObject req;
		req["model"] = model;
		req["input"] = QJsonArray{
			QJsonObject{{"role", "system"}, {"content", asInputText(systemPrompt)}},
			QJsonObject{{"role", "user"}, {"content", asInputText(userPayload)}}
		};
		req["text"] = QJsonObject{
			{"format", QJsonObject{
				{"type", "json_schema"},
				{"name", "debug_assistant"},
				{"schema", responseSchema()},
				{"strict", true}
			}}
		};
		req["temperature"] = 0.2;
		req["store"] = false;
		return QJsonDocument(req).toJson(QJsonDocument::Compact);
	}

	// Ollama: POST /api/chat with messages and JSON schema in "format".
	const QString model = s.value("ai/ollamaModel", "llama3.1").toString().trimmed();
	QJsonObject req;
	req["model"] = model;
	req["stream"] = false;
	req["messages"] = QJsonArray{
		QJsonObject{{"role", "system"}, {"content", systemPrompt}},
		QJsonObject{{"role", "user"}, {"content", userPayload}}
	};
	req["format"] = responseSchema();
	req["options"] = QJsonObject{{"temperature", 0.2}};
	return QJsonDocument(req).toJson(QJsonDocument::Compact);
}

void DebugAssistantDock::onReplyFinished(QNetworkReply* reply)
{
	if (!reply)
		return;

	const QByteArray payload = reply->readAll();
	if (reply->error() != QNetworkReply::NoError) {
		appendError(QString("Network error: %1").arg(reply->errorString()));
		if (!payload.isEmpty())
			appendError(QString("Error body: %1").arg(QString::fromUtf8(payload)));
		setBusy(false);
		reply->deleteLater();
		return;
	}

	const QString text = extractResponseText(payload);
	if (text.isEmpty()) {
		appendError("Empty response.");
		setBusy(false);
		reply->deleteLater();
		return;
	}

	appendAssistantText(text);
	populateActionsFromJsonText(text);
	setBusy(false);
	reply->deleteLater();
}

QString DebugAssistantDock::extractResponseText(const QByteArray& json) const
{
	QJsonParseError err{};
	const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
	if (err.error != QJsonParseError::NoError)
		return QString();

	// Ollama response: { message: { content: "..." } }
	if (doc.isObject()) {
		const QJsonObject o = doc.object();
		const QJsonObject msg = o.value("message").toObject();
		const QString content = msg.value("content").toString();
		if (!content.isEmpty())
			return content;
	}

	QJsonArray output;
	if (doc.isObject()) {
		const QJsonObject o = doc.object();
		output = o.value("output").toArray();
	} else if (doc.isArray()) {
		// Some callers/logs might pass just the "output" array portion.
		output = doc.array();
	} else {
		return QString();
	}

	for (const auto& itemV : output) {
		const QJsonObject item = itemV.toObject();
		if (item.value("type").toString() != "message")
			continue;
		const QJsonArray content = item.value("content").toArray();
		for (const auto& cV : content) {
			const QJsonObject c = cV.toObject();
			if (c.value("type").toString() == "output_text") {
				return c.value("text").toString();
			}
		}
	}
	return QString();
}

void DebugAssistantDock::populateActionsFromJsonText(const QString& text)
{
	QJsonParseError err{};
	const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject())
		return;

	m_actions->blockSignals(true);
	m_actions->clear();

	const QJsonObject root = doc.object();
	const QJsonArray actions = root.value("actions").toArray();
	for (const auto& aV : actions) {
		const QJsonObject a = aV.toObject();
		const QString type = a.value("type").toString();
		const QJsonArray argsArr = a.value("args").toArray();
		QJsonObject kv;
		for (const auto& kvV : argsArr) {
			const QJsonObject kvObj = kvV.toObject();
			const QString k = kvObj.value("key").toString();
			const QString v = kvObj.value("value").toString();
			if (!k.isEmpty())
				kv.insert(k, v);
		}
		const QString rationale = a.value("rationale").toString();

		auto* item = new QTreeWidgetItem(m_actions);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(0, Qt::Unchecked);
		item->setText(0, type);
		item->setText(1, QString::fromUtf8(QJsonDocument(kv).toJson(QJsonDocument::Compact)));
		item->setText(2, rationale);
		item->setData(0, Qt::UserRole, type);
		item->setData(0, Qt::UserRole + 1, kv);
	}
	m_actions->resizeColumnToContents(0);
	m_actions->resizeColumnToContents(1);
	m_actions->blockSignals(false);
	updateApplyEnabled();
}

void DebugAssistantDock::applySelectedActions()
{
	if (!m_session)
		return;

	for (int i = 0; i < m_actions->topLevelItemCount(); ++i) {
		auto* item = m_actions->topLevelItem(i);
		if (!item || item->checkState(0) != Qt::Checked)
			continue;

		const QString type = item->data(0, Qt::UserRole).toString();
		const QJsonObject args = item->data(0, Qt::UserRole + 1).toJsonObject();

		if (type == "evaluate") {
			m_session->evaluateExpression(args.value("expr").toString());
		} else if (type == "set_breakpoint") {
			m_session->insertBreakpoint(args.value("location").toString());
		} else if (type == "toggle_breakpoint") {
			m_session->toggleBreakpoint(args.value("location").toString());
		} else if (type == "run_to_cursor") {
			const QString file = args.value("file").toString();
			const int line = args.value("line").toString().toInt();
			m_session->runToCursor(QString("%1:%2").arg(file).arg(line));
		} else if (type == "select_frame") {
			m_session->selectStackFrame(args.value("index").toString().toInt());
		} else if (type == "step_into") {
			m_session->stepInto();
		} else if (type == "step_over") {
			m_session->stepOver();
		} else if (type == "step_out") {
			m_session->stepOut();
		} else if (type == "continue") {
			m_session->continueExecution();
		} else if (type == "interrupt") {
			m_session->interruptExecution();
		} else if (type == "set_variable") {
			m_session->setVariable(args.value("path").toString(),
			                       args.value("value").toString());
		} else if (type == "raw_command") {
			m_session->sendRawCommand(args.value("cmd").toString());
		}

		item->setCheckState(0, Qt::Unchecked);
	}

	appendInfo("Applied selected actions.");
	updateApplyEnabled();
}

void DebugAssistantDock::appendUser(const QString& text)
{
	const QString safe = Qt::convertFromPlainText(text).trimmed();
	m_log->append(QString("<div style='margin:6px 0'><b style='color:#9cdcfe'>You</b>: %1</div>").arg(safe));
}

void DebugAssistantDock::appendAssistantText(const QString& text)
{
	// Try to parse assistant JSON and render nicely.
	QJsonParseError err{};
	const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
	if (err.error == QJsonParseError::NoError && doc.isObject()) {
		const QJsonObject o = doc.object();
		const QString summary = o.value("summary").toString();
		const QJsonArray hypotheses = o.value("hypotheses").toArray();
		const QJsonArray evidence = o.value("evidence").toArray();

		QString html = "<div style='margin:8px 0'><b style='color:#c586c0'>Assistant</b></div>";
		if (!summary.isEmpty())
			html += QString("<div style='margin:4px 0'><b>Summary:</b> %1</div>")
			            .arg(Qt::convertFromPlainText(summary).trimmed());

		auto renderList = [](const QString& title, const QJsonArray& arr) {
			if (arr.isEmpty())
				return QString();
			QString out = QString("<div style='margin:6px 0'><b>%1:</b><ul style='margin:4px 0 4px 18px'>").arg(title);
			for (const auto& v : arr)
				out += QString("<li>%1</li>").arg(Qt::convertFromPlainText(v.toString()).trimmed());
			out += "</ul></div>";
			return out;
		};

		html += renderList("Hypotheses", hypotheses);
		html += renderList("Evidence", evidence);
		m_log->append(html);
		return;
	}

	// Fallback: raw text
	const QString safe = Qt::convertFromPlainText(text).trimmed();
	m_log->append(QString("<div style='margin:6px 0'><b style='color:#c586c0'>Assistant</b>: %1</div>").arg(safe));
}

void DebugAssistantDock::appendInfo(const QString& text)
{
	const QString safe = Qt::convertFromPlainText(text).trimmed();
	m_log->append(QString("<div style='margin:4px 0;color:rgba(255,255,255,160)'>%1</div>").arg(safe));
}

void DebugAssistantDock::appendError(const QString& text)
{
	const QString safe = Qt::convertFromPlainText(text).trimmed();
	m_log->append(QString("<div style='margin:4px 0;color:#f48771'><b>Error:</b> %1</div>").arg(safe));
}

void DebugAssistantDock::setBusy(bool busy)
{
	m_sendBtn->setEnabled(!busy);
	m_suggestBtn->setEnabled(!busy);
	m_applyBtn->setEnabled(!busy && m_applyBtn->isEnabled());

	QSettings s;
	const QString provider = s.value("ai/provider", "ollama").toString().trimmed();
	const QString model = provider == "openai"
		? s.value("ai/openaiModel", "gpt-4.1-mini").toString().trimmed()
		: s.value("ai/ollamaModel", "llama3.1").toString().trimmed();
	const QString status = busy ? tr("busy") : tr("ready");
	m_providerLabel->setText(QString("%1 · %2 · %3").arg(provider, model, status));
}

void DebugAssistantDock::updateApplyEnabled()
{
	bool anyChecked = false;
	for (int i = 0; i < m_actions->topLevelItemCount(); ++i) {
		auto* item = m_actions->topLevelItem(i);
		if (item && item->checkState(0) == Qt::Checked) {
			anyChecked = true;
			break;
		}
	}
	m_applyBtn->setEnabled(anyChecked);
}
