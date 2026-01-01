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

#include "VariablesView.h"

#include <QHeaderView>
#include <QStandardItem>
#include <QStandardItemModel>

VariablesView::VariablesView(QWidget *parent)
    : QTreeView(parent), m_model(new QStandardItemModel(this)) {
	m_model->setHorizontalHeaderLabels({tr("Name"), tr("Value"), tr("Type")});
	setModel(m_model);
	header()->setStretchLastSection(true);
	setAlternatingRowColors(true);
	setRootIsDecorated(true);
}

void VariablesView::setSession(DebugSession *session) {
	if (m_session == session)
		return;

	// disconnect from previous session if any
	if (m_session)
		disconnect(m_session, nullptr, this, nullptr);

	m_session = session;

	if (m_session) {
		// DebugSession emits a single signal when its data changes
		connect(m_session, &DebugSession::sessionUpdated, this,
		        &VariablesView::refresh);
	}
}

void VariablesView::clearVariables() {
	m_model->clear();
	m_model->setHorizontalHeaderLabels({tr("Name"), tr("Value"), tr("Type")});
}

void VariablesView::refresh() {
	if (!m_session)
		return;

	clearVariables();

	// usa le variabili FLAT di LLDB
	auto vars = m_session->variables();
	for (const VariableInfo &v : vars) {
		VarNode *n = new VarNode;
		n->name = v.name;
		n->value = v.value;
		n->type = v.type;
		n->hasChildren =
		    false; // children verranno creati dal parser in addNode()
		addNode(nullptr, n);
	}
}

void VariablesView::addNode(QStandardItem *parent, VarNode *node) {
	if (!node)
		return;

	QList<QStandardItem *> row;
	QStandardItem *nameItem = new QStandardItem(node->name);
	QStandardItem *valueItem = new QStandardItem(node->value);
	QStandardItem *typeItem = new QStandardItem(node->type);

	row << nameItem << valueItem << typeItem;

	if (parent)
		parent->appendRow(row);
	else
		m_model->appendRow(row);

	//
	// FALLBACK: LLDB returns struct like "{a = 1, b = 2}" without children.
	// If DebugSession did not populate node->children, we generate them on the
	// fly.
	//
	bool shouldParseStruct = node->children.isEmpty() &&
	                         node->value.startsWith("{") &&
	                         node->value.endsWith("}");

	if (shouldParseStruct) {
		QString inside =
		    node->value.mid(1, node->value.length() - 2); // strip {}
		QStringList fields = inside.split(",", Qt::SkipEmptyParts);

		for (QString f : fields) {
			QStringList parts = f.split("=", Qt::SkipEmptyParts);
			if (parts.size() == 2) {
				VarNode *c = new VarNode;
				c->name = parts[0].trimmed();
				c->value = parts[1].trimmed();
				c->type = ""; // LLDB does not provide member type
				c->hasChildren = false;
				node->children.append(c);
			}
		}
	}

	// Render children (either coming from DebugSession OR parsed above)
	for (VarNode *child : node->children)
		addNode(nameItem, child);
}
