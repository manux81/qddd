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

#include "BreakpointsView.h"
#include <QHeaderView>

BreakpointsView::BreakpointsView(QWidget *parent)
    : QTreeView(parent), m_model(new QStandardItemModel(this)) {
	m_model->setHorizontalHeaderLabels({tr("Location")});
	setModel(m_model);
	header()->setStretchLastSection(true);

	connect(this, &QTreeView::clicked, this, [this](const QModelIndex &idx) {
		QString loc = m_model->item(idx.row())->text();
		emit breakpointSelected(loc);
	});
}

void BreakpointsView::setSession(DebugSession *session) {
	m_session = session;
	if (m_session == nullptr)
		return;

	connect(m_session, &DebugSession::breakpointsChanged, this,
	        &BreakpointsView::onBpListChanged);
}

void BreakpointsView::onBpListChanged(const QList<BreakpointInfo> &list) {
	m_model->removeRows(0, m_model->rowCount());

	for (const auto &bp : list) {
		QString loc = QString("%1:%2").arg(bp.file).arg(bp.line);
		QList<QStandardItem *> row;
		row << new QStandardItem(loc);
		m_model->appendRow(row);
	}
}

void BreakpointsView::refresh() {
	if (!m_session)
		return;

	// optional: ask DebugSession for list once we implement real BP list
	// retrieval
}
