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
#include "DebugSession.h"
#include "BreakpointEditorDialog.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QMenu>

// ============================================================================
// ctor
// ============================================================================

BreakpointsView::BreakpointsView(QWidget *parent) : QTreeView(parent) {
	setupModel();
	setupView();

	connect(this, &QTreeView::activated, this, &BreakpointsView::onActivated);

	connect(m_model, &QStandardItemModel::itemChanged, this,
	        &BreakpointsView::onItemChanged);
}

// ============================================================================
// Session
// ============================================================================

void BreakpointsView::setSession(DebuggerSession *session) {
	if (m_session == session)
		return;

	if (m_session)
		disconnect(m_session, nullptr, this, nullptr);

	m_session = session;

	if (!m_session)
		return;

	connect(m_session, &DebuggerSession::breakpointsUpdated, this,
	        &BreakpointsView::refresh);

	refresh();
}

// ============================================================================
// Model / View
// ============================================================================

void BreakpointsView::setupModel() {
	m_model = new QStandardItemModel(this);
	m_model->setColumnCount(ColCount);
	m_model->setHorizontalHeaderLabels({"", "#", tr("Name"), tr("Labels"),
	                                    tr("Condition"), tr("Hit Count"),
	                                    tr("State")});

	setModel(m_model);
}

void BreakpointsView::setupView() {
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setAlternatingRowColors(true);
	setRootIsDecorated(false);
	setSortingEnabled(true);

	header()->setSectionsClickable(true);
	header()->setSortIndicatorShown(true);
	header()->setStretchLastSection(true);

	setStyleSheet("QTreeView::item { height: 22px; }");

	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this,
	        &BreakpointsView::showContextMenu);
}

// ============================================================================
// Icons
// ============================================================================

QIcon BreakpointsView::iconFor(const BreakpointInfo &bp) const {
	if (bp.pending)
		return QIcon(
		    ":/icons/resources/icons/debug-breakpoint-log-unverified.svg");
	if (!bp.enabled)
		return QIcon(":/icons/resources/icons/breakpoint-disabled.svg");
	return QIcon(":/icons/resources/icons/debug-breakpoint-log.svg");
}

// ============================================================================
// Refresh
// ============================================================================

void BreakpointsView::refresh() {
	if (!m_session)
		return;

	rebuild(m_session->breakpoints());
}

void BreakpointsView::rebuild(const QVector<BreakpointInfo> &list) {
	m_blockItemChanged = true;
	m_model->removeRows(0, m_model->rowCount());

	for (const auto &bp : list) {

		QList<QStandardItem *> row;

		// Enabled (icon only, VS-style)
		auto *enabledItem = new QStandardItem();
		enabledItem->setEditable(false);
		enabledItem->setIcon(iconFor(bp));
		enabledItem->setData(bp.number, RoleBkptNumber);

		// Number
		auto *numItem = new QStandardItem(QString::number(bp.number));
		numItem->setEditable(false);
		numItem->setData(bp.number, RoleSortValue);

		// Location
		const QString loc =
		    QString("%1:%2").arg(QFileInfo(bp.file).fileName()).arg(bp.line);
		auto *locItem = new QStandardItem(loc);
		locItem->setEditable(false);
		locItem->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

		// Labels: function name
		auto *labelItem = new QStandardItem(bp.function);
		labelItem->setEditable(false);

		// Condition
		auto *condItem = new QStandardItem(bp.condition);
		condItem->setEditable(false);

		// Hit count
		auto *hitItem = new QStandardItem(QString::number(bp.hitCount));
		hitItem->setEditable(false);
		hitItem->setData(bp.hitCount, RoleSortValue);

		// Labels
		auto *tempItem = new QStandardItem(bp.temporary ? "Temp" : "");
		labelItem->setEditable(false);

		row << enabledItem << numItem << locItem << labelItem << condItem
		    << hitItem << tempItem;

		m_model->appendRow(row);
	}

	m_blockItemChanged = false;

	for (int c = 0; c < ColCount; ++c)
		resizeColumnToContents(c);
}

// ============================================================================
// Interaction
// ============================================================================

void BreakpointsView::onActivated(const QModelIndex &index) {
	if (!index.isValid())
		return;

	const QString loc = m_model->item(index.row(), ColLocation)->text();

	if (!loc.isEmpty())
		emit breakpointSelected(loc);
}

void BreakpointsView::onItemChanged(QStandardItem *) {
	// non usato: niente checkbox Qt
}

// ============================================================================
// Context menu
// ============================================================================

void BreakpointsView::showContextMenu(const QPoint &pos) {
	if (!m_session)
		return;

	QModelIndex idx = indexAt(pos);
	if (!idx.isValid())
		return;

	const int row = idx.row();
	auto *enabledItem = m_model->item(row, ColEnabled);

	const int number = enabledItem->data(RoleBkptNumber).toInt();

	if (number <= 0)
		return;

	QMenu menu(this);
	menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint);
	menu.setAttribute(Qt::WA_TranslucentBackground);
	menu.setMinimumWidth(220);

	QAction *actEnable = menu.addAction(tr("Enable Breakpoint"));
	QAction *actDisable = menu.addAction(tr("Disable Breakpoint"));
	QAction *actCondition = menu.addAction(tr("Edit Breakpoint…"));
	QAction *actDelete = menu.addAction(tr("Delete Breakpoint"));
	menu.setStyleSheet(R"(
  QWidget {
			background: rgba(30, 30, 30, 220);
			border: 1px solid #555;
			border-radius: 6px;
		}

		QToolButton {
			color: white;
			background: transparent;
			border: none;
			font-size: 16px;
		}

		QToolButton:hover {
			background: #3a3a3a;
			border-radius: 4px;
		}

		QToolButton:pressed {
			background: #5a5a5a;
		}
	)");
	QAction *chosen = menu.exec(viewport()->mapToGlobal(pos));

	if (!chosen)
		return;

	if (chosen == actDelete)
		m_session->removeBreakpoint(number);
	else if (chosen == actEnable)
		m_session->setBreakpointEnabled(number, true);
	else if (chosen == actDisable)
		m_session->setBreakpointEnabled(number, false);
	else if (chosen == actCondition) {
		auto* dlg = new BreakpointEditorDialog(m_session, 0, this);
		dlg->move(QCursor::pos());
		dlg->show();
	}
}
