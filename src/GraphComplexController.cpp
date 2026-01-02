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

#include "GraphComplexController.h"

GraphComplexController::GraphComplexController(DebugSession *session,
                                               GraphicalVariablesView *view,
                                               QObject *parent)
    : QObject(parent), m_session(session), m_view(view) {
	connect(m_session, &DebugSession::complexVariablesUpdated, this,
	        &GraphComplexController::onComplexVars);

	connect(m_view, &GraphicalVariablesView::nodeDoubleClicked, this,
	        &GraphComplexController::onNodeDblClicked);
}

void GraphComplexController::onComplexVars(QList<VarNode *> roots) {
	QVector<GraphNode> nodes;
	QVector<GraphEdge> edges;

	for (VarNode *root : roots)
		walk(root, nodes, edges, QString());

	m_view->setGraph(nodes, edges);
}

void GraphComplexController::walk(VarNode *n, QVector<GraphNode> &nodes,
                                  QVector<GraphEdge> &edges,
                                  const QString &parentId) {
	if (!n)
		return;

	GraphNode g;
	g.id = !n->varId.isEmpty()
	           ? n->varId
	           : QString("node_%1")
	                 .arg(reinterpret_cast<quintptr>(n), 0, 16);
	g.title = n->name;
	g.color = typeToColor(n->type);
	g.fields = {
	    { "type",  "", n->type  },
	    { "value", "", n->value }
	};


	nodes.push_back(g);

	if (!parentId.isEmpty()) {
		GraphEdge e;
		e.fromId = parentId;
		e.toId = g.id;
		e.label = QString();
		edges.push_back(e);
	}

	for (VarNode *child : n->children)
		walk(child, nodes, edges, g.id);
}

QColor GraphComplexController::typeToColor(const QString &type) {
	const QString t = type.toLower();
	if (t.contains("int"))
		return QColor("#BBDEFB"); // blu chiaro
	if (t.contains("float") || t.contains("double"))
		return QColor("#FFCCBC"); // arancio chiaro
	if (t.contains("char") || t.contains("string"))
		return QColor("#C8E6C9"); // verde chiaro
	if (t.contains('*') || t.contains("&"))
		return QColor("#F8BBD0"); // rosa
	return QColor("#E0E0E0");
}

void GraphComplexController::onNodeDblClicked(const QString &id) {
	// Hook per future espansioni lazy, per ora non facciamo nulla.
	// Esempio eventuale:
	// m_session->evaluateExpression(...);
	Q_UNUSED(id);
}
