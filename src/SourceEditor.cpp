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

#include "SourceEditor.h"
#include <QFile>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>

LineNumberArea::LineNumberArea(SourceEditor *editor)
    : QWidget(editor), m_editor(editor) {}

QSize LineNumberArea::sizeHint() const {
	return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event) {
	m_editor->lineNumberAreaPaintEvent(event);
}

SourceEditor::SourceEditor(QWidget *parent)
    : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this)) {
	connect(this, &SourceEditor::blockCountChanged, this,
	        &SourceEditor::updateLineNumberAreaWidth);
	connect(this, &SourceEditor::updateRequest, this,
	        &SourceEditor::updateLineNumberArea);
	connect(this, &SourceEditor::cursorPositionChanged, this,
	        &SourceEditor::highlightCurrentLine);

	updateLineNumberAreaWidth(0);

	QFont f("Menlo");
	f.setPointSize(14);
	setFont(f);

	setStyleSheet("QPlainTextEdit {"
	              " background: #1e1e1e;"
	              " color: #e0e0e0;"
	              " selection-background-color: #264f78;"
	              "}");

	highlightCurrentLine();
}

void SourceEditor::setBreakpointLines(const QSet<int>& lines)
{
	const QString file =
		property("currentFile").toString();

	for (int line : lines) {
		const QString location = QString("%1:%2").arg(file).arg(line);
		m_session->insertBreakpoint(location);
	}

	update();
}


void SourceEditor::showLocation(const QString &file, int line) {
	setProperty("currentFile", file);

	QFile f(file);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return;

	setPlainText(QString::fromUtf8(f.readAll()));

	QTextCursor cursor(document()->findBlockByLineNumber(line - 1));
	setTextCursor(cursor);
	centerCursor();

	highlightCurrentLine();

	if (m_lineNumberArea)
		m_lineNumberArea->update();
}

void SourceEditor::highlightCurrentLine() {
	QList<QTextEdit::ExtraSelection> extra;

	QTextEdit::ExtraSelection sel;
	sel.format.setBackground(QColor("#3c3c3c"));
	sel.format.setForeground(Qt::white);
	sel.format.setProperty(QTextFormat::FullWidthSelection, true);

	sel.cursor = textCursor();
	sel.cursor.clearSelection();
	extra << sel;

	setExtraSelections(extra);
}

void SourceEditor::setSession(DebuggerSession* session)
{
	if (m_session == session)
		return;

	if (m_session)
		disconnect(m_session, nullptr, this, nullptr);

	m_session = session;
	if (!m_session)
		return;

	connect(m_session, &DebuggerSession::stoppedAt,
			this,
			[this](const QString& file, int line, const QString&) {
				showLocation(file, line);
				setCurrentPC(line);
			});
}


int SourceEditor::lineNumberAreaWidth() {
	int digits = 1;
	int max = qMax(1, blockCount());
	while (max >= 10) {
		max /= 10;
		++digits;
	}

	const int space =
	    3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
	return space + 16;
}

void SourceEditor::updateLineNumberAreaWidth(int) {
	setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void SourceEditor::updateLineNumberArea(const QRect &rect, int dy) {
	if (dy)
		m_lineNumberArea->scroll(0, dy);
	else
		m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(),
		                         rect.height());

	if (rect.contains(viewport()->rect()))
		updateLineNumberAreaWidth(0);
}

void SourceEditor::resizeEvent(QResizeEvent *event) {
	QPlainTextEdit::resizeEvent(event);

	QRect cr = contentsRect();
	m_lineNumberArea->setGeometry(
	    QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void SourceEditor::lineNumberAreaPaintEvent(QPaintEvent *event) {
	QPainter painter(m_lineNumberArea);
	painter.fillRect(event->rect(), QColor("#252525"));

	QTextBlock block = firstVisibleBlock();
	int blockNumber = block.blockNumber();
	int top = static_cast<int>(
	    blockBoundingGeometry(block).translated(contentOffset()).top());
	int bottom = top + static_cast<int>(blockBoundingRect(block).height());

	const int iconX = 2;
	const int iconSize = fontMetrics().height() - 2;
	const QString curFile = property("currentFile").toString();

	while (block.isValid() && top <= event->rect().bottom()) {
		if (block.isVisible() && bottom >= event->rect().top()) {

			int lineIndex = blockNumber + 1;

			const bool hasBp = std::any_of(
				m_session->breakpoints().cbegin(),
				m_session->breakpoints().cend(),
				[lineIndex](const BreakpointInfo& bp) {
					return bp.line == lineIndex;
				}
			);

			if (hasBp) {
				painter.setBrush(QColor("#cc2222"));
				painter.setPen(Qt::NoPen);
				painter.drawEllipse(iconX, top + 4, iconSize / 2, iconSize / 2);
			}

			if (lineIndex == m_currentPC) {
				painter.setRenderHint(QPainter::Antialiasing);
				painter.setBrush(QColor("#ffd700"));
				painter.setPen(Qt::NoPen);

				QPointF p1(iconX + 8, top + iconSize / 2);
				QPointF p2(iconX, top + iconSize / 2 - 6);
				QPointF p3(iconX, top + iconSize / 2 + 6);

				painter.drawPolygon(QPolygonF({p1, p2, p3}));
			}

			// --- NUMERO DI RIGA ---
			QString number = QString::number(lineIndex);
			painter.setPen(QColor("#0099dd"));
			painter.drawText(0, top, m_lineNumberArea->width() - 2,
			                 fontMetrics().height(), Qt::AlignRight, number);
		}

		block = block.next();
		top = bottom;
		bottom = top + static_cast<int>(blockBoundingRect(block).height());
		++blockNumber;
	}
}

void SourceEditor::setCurrentPC(int line) {
	m_currentPC = line;

	if (m_lineNumberArea)
		m_lineNumberArea->update();

	highlightCurrentLine();
}

void SourceEditor::mousePressEvent(QMouseEvent *event)
{
    QPlainTextEdit::mousePressEvent(event);

    QTextCursor c = cursorForPosition(event->pos());
    int line = c.blockNumber() + 1;

    QString file;
    int dummy;
    currentLocation(file, dummy);

    if (file.isEmpty())
        return;

    if (event->button() == Qt::LeftButton &&
        event->x() < m_lineNumberArea->width()) {

        emit toggleBreakpointRequested(file, line);
        return;
    }

    if (event->button() == Qt::RightButton) {
        emit runUntilRequested(file, line);
        return;
    }
}


void SourceEditor::currentLocation(QString &file, int &line) const {
	file = this->property("currentFile")
	           .toString();
	line = textCursor().blockNumber() + 1;
}

void SourceEditor::setBreakpointsUpdated(const QSet<int>& lines)
{
    viewport()->update();
}

