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
 * AND ANY EXPRESS IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
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

#include "DisassemblyView.h"

#include "DebugSession.h"

#include <QHeaderView>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

static bool parseHexAddr(const QString& s, qulonglong* out)
{
	if (!out)
		return false;
	QString t = s.trimmed();
	if (t.startsWith("▶"))
		t = t.mid(1).trimmed();
	if (t.startsWith("0x") || t.startsWith("0X"))
		t = t.mid(2);
	if (t.isEmpty())
		return false;

	bool ok = false;
	const qulonglong v = t.toULongLong(&ok, 16);
	if (!ok)
		return false;
	*out = v;
	return true;
}

DisassemblyView::DisassemblyView(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	m_table = new QTableWidget(this);
	m_table->setColumnCount(3);
	m_table->setHorizontalHeaderLabels({tr("Address"), tr("Bytes"), tr("Instruction")});
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setShowGrid(false);
	m_table->verticalHeader()->setVisible(false);
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	m_table->setAlternatingRowColors(true);
	m_table->setWordWrap(false);
	m_table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	m_table->setStyleSheet("QScrollBar:vertical { background: transparent; width: 10px; margin: 10px 4px 10px 4px; }"
	                       "QScrollBar::handle:vertical { background: #CBD5E1; border-radius: 5px; min-height: 32px; }"
	                       "QScrollBar::handle:vertical:hover { background: #94A3B8; }"
	                       "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
	                       "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
	                       "QScrollBar:horizontal { background: transparent; height: 10px; margin: 4px 10px 4px 10px; }"
	                       "QScrollBar::handle:horizontal { background: #CBD5E1; border-radius: 5px; min-width: 32px; }"
	                       "QScrollBar::handle:horizontal:hover { background: #94A3B8; }"
	                       "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
	                       "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }");

	layout->addWidget(m_table);
	setLayout(layout);

	rebuildTableFromText(tr("Disassembly will appear here when the target stops."));
}

void DisassemblyView::setSession(DebuggerSession* session)
{
	if (m_session) {
		disconnect(m_session, nullptr, this, nullptr);
		m_session = nullptr;
	}

	m_session = session;
	if (!m_session)
		return;

	connect(m_session, &DebuggerSession::disassemblyUpdated,
	        this, &DisassemblyView::setDisassemblyText);
	connect(m_session, &DebuggerSession::stoppedAtAddress,
	        this, &DisassemblyView::setCurrentAddress);

	// Auto-refresh disassembly on stop; let DebuggerSession decide the exact MI command.
	connect(m_session, &DebuggerSession::stoppedAt, this,
	        [this](const QString& file, int line, const QString&) {
		        if (m_session && m_autoRefreshEnabled)
			        m_session->requestDisassembly(file, line);
	        });
}

void DisassemblyView::setAutoRefreshEnabled(bool enabled)
{
	m_autoRefreshEnabled = enabled;
	if (m_autoRefreshEnabled && m_session)
		m_session->requestDisassemblyAtLastStop();
}

void DisassemblyView::setDisassemblyText(const QString& text)
{
	if (!m_autoRefreshEnabled)
		return;
	rebuildTableFromText(text);
}

void DisassemblyView::setCurrentAddress(const QString& addr)
{
	if (!m_autoRefreshEnabled)
		return;
	m_currentAddr = addr.trimmed();
	// Re-apply highlight to current contents (if present).
	rebuildTableFromText(QString());
}

void DisassemblyView::rebuildTableFromText(const QString& text)
{
	if (!m_table)
		return;

	// If text is empty, just re-highlight existing rows.
	const bool replace = !text.isNull() && !text.isEmpty();
	if (replace) {
		m_table->setRowCount(0);

		const QStringList lines = text.split('\n');
		const QRegularExpression reLine(
			// Accept common disassembly formats:
			// "0xADDR ...", "-> 0xADDR: ...", "0xADDR <...>: ..."
			R"(^\s*(?:->\s*)?(0x[0-9a-fA-F]+)\s*:?\s*(.*)\s*$)");
		const QRegularExpression reBytesPrefix(
			R"(^((?:[0-9a-fA-F]{2}\s+){2,}[0-9a-fA-F]{2})\s+(.*)$)");

		for (const QString& raw : lines) {
			const QString line = raw.trimmed();
			if (line.isEmpty())
				continue;
			if (line.startsWith("#") || line.startsWith("Summary:") || line.startsWith("Hypotheses:"))
				continue;
			if (line.contains("^done") || line.contains("^error"))
				continue;

			const auto m = reLine.match(line);
			if (!m.hasMatch())
				continue;

			const QString addr = m.captured(1);
			QString rest = m.captured(2).trimmed();

			QString bytes;
			QString inst = rest;

			// LLDB often prints: "<+0>: ..." prefix.
			if (inst.startsWith('<')) {
				const int colon = inst.indexOf(':');
				if (colon > 0 && colon < 16)
					inst = inst.mid(colon + 1).trimmed();
			}

			// Preferred format uses tabs: "addr\tbytes?\tinst"
			if (rest.contains('\t')) {
				const QStringList parts = rest.split('\t', Qt::SkipEmptyParts);
				// Heuristics:
				// - If we have 3 parts: [maybe symbol], [bytes/opcode], [inst]
				// - If we have 2 parts: [bytes/opcode], [inst] OR [symbol], [inst]
				auto looksLikeBytes = [](const QString& s) {
					const QRegularExpression re(R"(^([0-9a-fA-F]{2}\s+){2,}[0-9a-fA-F]{2}$)");
					return re.match(s.trimmed()).hasMatch();
				};
				if (parts.size() >= 3) {
					const QString p1 = parts[0].trimmed();
					const QString p2 = parts[1].trimmed();
					const QString p3 = parts[2].trimmed();
					if (looksLikeBytes(p2)) {
						bytes = p2;
						inst = p3;
					} else {
						inst = p3;
					}
				} else if (parts.size() == 2) {
					const QString p1 = parts[0].trimmed();
					const QString p2 = parts[1].trimmed();
					if (looksLikeBytes(p1)) {
						bytes = p1;
						inst = p2;
					} else {
						inst = p2;
					}
				}
			} else {
				const auto mb = reBytesPrefix.match(rest);
				if (mb.hasMatch()) {
					bytes = mb.captured(1).trimmed();
					inst = mb.captured(2).trimmed();
				}
			}

			const int row = m_table->rowCount();
			m_table->insertRow(row);
			auto* addrItem = new QTableWidgetItem(addr);
			addrItem->setData(Qt::UserRole, addr); // raw
			qulonglong addrNum = 0;
			if (parseHexAddr(addr, &addrNum))
				addrItem->setData(Qt::UserRole + 1, QVariant::fromValue(addrNum)); // numeric
			m_table->setItem(row, 0, addrItem);
			m_table->setItem(row, 1, new QTableWidgetItem(bytes));
			m_table->setItem(row, 2, new QTableWidgetItem(inst));
		}

		// Fallback: if we couldn't parse any rows, show the raw text so the user
		// understands what's happening.
		if (m_table->rowCount() == 0) {
			m_table->insertRow(0);
			m_table->setItem(0, 0, new QTableWidgetItem(QString()));
			m_table->setItem(0, 1, new QTableWidgetItem(QString()));
			auto* rawItem = new QTableWidgetItem(text.trimmed());
			rawItem->setFlags(rawItem->flags() & ~Qt::ItemIsSelectable);
			m_table->setItem(0, 2, rawItem);
		}
	}

	// Highlight current PC row (best-effort).
	qulonglong pcNum = 0;
	const bool hasPcNum = parseHexAddr(m_currentAddr, &pcNum);

	const QColor pcBg(0x66, 0x5b, 0x00);  // readable "gold" on dark themes
	const QColor pcFg(0xf1, 0xf1, 0xf1);
	QFont pcFont = m_table->font();
	pcFont.setBold(true);

	int matchRow = -1;
	for (int r = 0; r < m_table->rowCount(); ++r) {
		auto* addrItem = m_table->item(r, 0);
		if (!addrItem)
			continue;

		const QString rawAddr = addrItem->data(Qt::UserRole).toString();
		bool isPc = false;
		if (hasPcNum) {
			const qulonglong rowNum = addrItem->data(Qt::UserRole + 1).toULongLong();
			isPc = (rowNum != 0 && rowNum == pcNum);
		} else if (!m_currentAddr.isEmpty()) {
			isPc = rawAddr.trimmed().compare(m_currentAddr.trimmed(), Qt::CaseInsensitive) == 0;
		}

		// Restore base text (without marker) first.
		addrItem->setText(rawAddr);

		for (int c = 0; c < m_table->columnCount(); ++c) {
			auto* cell = m_table->item(r, c);
			if (!cell)
				continue;
			if (isPc) {
				cell->setBackground(pcBg);
				cell->setForeground(pcFg);
				cell->setFont(pcFont);
			} else {
				cell->setBackground(QBrush());
				cell->setForeground(QBrush());
				cell->setFont(m_table->font());
			}
		}

		if (isPc) {
			addrItem->setText(QStringLiteral("▶ ") + rawAddr);
			matchRow = r;
		}
	}

	if (matchRow >= 0) {
		m_table->setCurrentCell(matchRow, 2);
		m_table->scrollToItem(m_table->item(matchRow, 0), QAbstractItemView::PositionAtCenter);
	}
}
