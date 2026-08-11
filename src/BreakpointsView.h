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

#include <QTreeView>
#include <QStandardItemModel>

class DebuggerSession;

struct BreakpointInfo;

class BreakpointsView : public QTreeView
{
	Q_OBJECT
public:
	explicit BreakpointsView(QWidget *parent = nullptr);

	void setSession(DebuggerSession *session);

signals:
	void breakpointSelected(const QString &location);

private slots:
	void refresh();
	void onActivated(const QModelIndex &index);
	void onItemChanged(QStandardItem *item);
	void showContextMenu(const QPoint &pos);

private:
	enum Column {
		ColEnabled,
		ColNumber,
		ColLocation,
		ColCondition,
		ColHitCount,
		ColState,
		ColCount
	};

	enum {
		RoleBkptNumber = Qt::UserRole + 1,
		RoleSortValue,
		RoleLocationFile,
		RoleLocationLine
	};

	void setupModel();
	void setupView();
	void rebuild(const QVector<BreakpointInfo> &list);

	QIcon iconFor(const BreakpointInfo &bp) const;

	QStandardItemModel *m_model = nullptr;
	DebuggerSession *m_session = nullptr;
	bool m_blockItemChanged = false;
};

