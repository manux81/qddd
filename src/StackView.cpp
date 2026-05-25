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

#include "StackView.h"
#include <QHeaderView>
#include <QStandardItem>
#include <QStandardItemModel>

StackView::StackView(QWidget *parent)
    : QTreeView(parent), m_model(new QStandardItemModel(this)) {
	m_model->setHorizontalHeaderLabels(
	    {tr("#"), tr("Function"), tr("File"), tr("Line")});
	setModel(m_model);
	header()->setStretchLastSection(true);
	setRootIsDecorated(false);
	setAlternatingRowColors(true);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setStyleSheet("QScrollBar:vertical { background: transparent; width: 10px; margin: 10px 4px 10px 4px; }"
	              "QScrollBar::handle:vertical { background: #CBD5E1; border-radius: 5px; min-height: 32px; }"
	              "QScrollBar::handle:vertical:hover { background: #94A3B8; }"
	              "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
	              "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
	              "QScrollBar:horizontal { background: transparent; height: 10px; margin: 4px 10px 4px 10px; }"
	              "QScrollBar::handle:horizontal { background: #CBD5E1; border-radius: 5px; min-width: 32px; }"
	              "QScrollBar::handle:horizontal:hover { background: #94A3B8; }"
	              "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
	              "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }");

	connect(this, &QTreeView::activated, this, &StackView::onItemActivated);
}

void StackView::setSession(DebuggerSession *session) {
	m_session = session;
	if (m_session) {
		connect(m_session, &DebuggerSession::stackFramesUpdated, this,
		        &StackView::refresh);
	}
}

void StackView::clearFrames() { m_model->removeRows(0, m_model->rowCount()); }

void StackView::refresh() {
	if (!m_session)
		return;

	clearFrames();
	const auto frames = m_session->stackFrames();
	for (const auto &f : frames) {
		QList<QStandardItem *> row;
		row << new QStandardItem(f.level) << new QStandardItem(f.function)
		    << new QStandardItem(f.file)
		    << new QStandardItem(QString::number(f.line));
		m_model->appendRow(row);
	}
}

void StackView::onItemActivated(const QModelIndex &index) {
	if (!index.isValid())
		return;

	int row = index.row();
	QString file = m_model->item(row, 2)->text();
	int line = m_model->item(row, 3)->text().toInt();

	emit frameActivated(file, line);
}

int StackView::currentFrameIndex() const {
	QModelIndex idx = currentIndex();
	if (!idx.isValid())
		return 0;
	return idx.row();
}

void StackView::selectFrame(int index) {
	if (!m_model)
		return;

	QModelIndex idx = m_model->index(index, 0);
	if (idx.isValid()) {
		setCurrentIndex(idx);
		onItemActivated(idx);
	}
}
