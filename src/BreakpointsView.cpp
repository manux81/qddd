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
#include "DebugSession.h" // DebugSession + BreakpointInfo

#include <QAction>
#include <QHeaderView>
#include <QMenu>

BreakpointsView::BreakpointsView(QWidget *parent) : QTreeView(parent) {
	setupModel();
	setupView();

	connect(this, &QTreeView::activated, this, &BreakpointsView::onActivated);

	connect(m_model, &QStandardItemModel::itemChanged, this,
	        &BreakpointsView::onItemChanged);
}

void BreakpointsView::setSession(DebugSession *session) {
	if (m_session == session)
		return;

	if (m_session) {
		disconnect(m_session, nullptr, this, nullptr);
	}

	m_session = session;

	if (!m_session)
		return;

	// 1) aggiornamenti push dal backend
	connect(m_session, &DebugSession::breakpointsChanged, this,
	        &BreakpointsView::onBreakpointsChanged);

	// 2) primo popolamento: se la sessione ha già stato, mostralo subito
	onBreakpointsChanged(m_session->breakpoints());
}

void BreakpointsView::setupModel() {
	m_model = new QStandardItemModel(this);
	m_model->setColumnCount(ColCount);
	m_model->setHorizontalHeaderLabels(
	    {tr(""), tr("#"), tr("Source:Line"), tr("File"), tr("Line")});
	setModel(m_model);
}

void BreakpointsView::setupView() {
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setAlternatingRowColors(true);
	setRootIsDecorated(false);
	setSortingEnabled(true);

	header()->setStretchLastSection(true);
	header()->setSectionResizeMode(QHeaderView::Interactive);

	header()->resizeSection(ColEnabled, 50);
	header()->resizeSection(ColNumber, 44);
	header()->resizeSection(ColLocation, 260);

	// colonne tecniche: puoi nasconderle se vuoi solo la colonna "Source:Line"
	// (qui le lasciamo visibili per debug/completezza)
	setColumnHidden(ColLocation, true);
	// setColumnHidden(ColLine, true);

	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this,
	        &BreakpointsView::showContextMenu);
}

QIcon BreakpointsView::iconEnabled() const {
	QIcon ico(":/icons/resources/icons/debug-breakpoint-log.svg");
	return ico;
}

QIcon BreakpointsView::iconDisabled() const {
	QIcon ico(":/icons/resources/icons/debug-breakpoint-log-unverified.svg");
	return ico;
}

void BreakpointsView::onBreakpointsChanged(const QList<BreakpointInfo> &list) {
	rebuild(list);
}

void BreakpointsView::rebuild(const QList<BreakpointInfo> &list) {
	m_blockItemChanged = true;
	m_model->removeRows(0, m_model->rowCount());

	for (const auto &bp : list) {
		QList<QStandardItem *> row;

		// Enabled
		auto *enabledItem = new QStandardItem();
		enabledItem->setEditable(false);
		enabledItem->setCheckable(true);
		enabledItem->setCheckState(bp.enabled ? Qt::Checked : Qt::Unchecked);
		enabledItem->setData(bp.number, RoleBkptNumber);
		enabledItem->setIcon(bp.enabled ? iconEnabled() : iconDisabled());

		// Number
		auto *numItem = new QStandardItem(QString::number(bp.number));
		numItem->setEditable(false);

		// Source:Line (questo è quello che vuoi emettere in breakpointSelected)
		const QString loc = QString("%1:%2").arg(bp.file).arg(bp.line);
		auto *locItem = new QStandardItem(loc);
		locItem->setEditable(false);

		// File / Line separati (opzionali ma utili)
		auto *fileItem = new QStandardItem(bp.file);
		fileItem->setEditable(false);

		auto *lineItem = new QStandardItem(QString::number(bp.line));
		lineItem->setEditable(false);

		row << enabledItem << numItem << locItem << fileItem << lineItem;
		m_model->appendRow(row);
	}

	m_blockItemChanged = false;
	for (int c = 0; c < ColCount; ++c)
		resizeColumnToContents(c);
}

void BreakpointsView::onActivated(const QModelIndex &index) {
	if (!index.isValid())
		return;

	const int row = index.row();
	const QString loc = m_model->item(row, ColLocation)->text();
	if (!loc.isEmpty())
		emit breakpointSelected(loc);
}

void BreakpointsView::onItemChanged(QStandardItem *item) {
	if (m_blockItemChanged)
		return;

	if (!m_session || !item)
		return;

	if (item->column() != ColEnabled)
		return;

	const int number = item->data(RoleBkptNumber).toInt();
	if (number <= 0)
		return;

	const bool en = (item->checkState() == Qt::Checked);
	item->setIcon(en ? iconEnabled() : iconDisabled());

	m_session->toggleBreakpointEnabled(number, en);
}

void BreakpointsView::showContextMenu(const QPoint &pos) {
	QModelIndex idx = indexAt(pos);
	if (!idx.isValid() || !m_session)
		return;

	const int row = idx.row();
	auto *enabledItem = m_model->item(row, ColEnabled);
	const int number = enabledItem->data(RoleBkptNumber).toInt();
	if (number <= 0)
		return;

	const bool currentlyEnabled = (enabledItem->checkState() == Qt::Checked);

	QMenu menu(this);
	QAction *actEnable = menu.addAction(tr("Enable"));
	QAction *actDisable = menu.addAction(tr("Disable"));
	QAction *actDelete = menu.addAction(tr("Delete"));

	actEnable->setEnabled(!currentlyEnabled);
	actDisable->setEnabled(currentlyEnabled);

	QAction *chosen = menu.exec(viewport()->mapToGlobal(pos));
	if (!chosen)
		return;

	if (chosen == actDelete) {
		m_session->deleteBreakpoint(number);
		return;
	}

	if (chosen == actEnable) {
		m_blockItemChanged = true;
		enabledItem->setCheckState(Qt::Checked);
		enabledItem->setIcon(iconEnabled());
		m_blockItemChanged = false;

		m_session->toggleBreakpointEnabled(number, true);
		return;
	}

	if (chosen == actDisable) {
		m_blockItemChanged = true;
		enabledItem->setCheckState(Qt::Unchecked);
		enabledItem->setIcon(iconDisabled());
		m_blockItemChanged = false;

		m_session->toggleBreakpointEnabled(number, false);
		return;
	}
}
