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
#include <QFileInfo>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QScrollBar>
#include <QTextBlock>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QPlainTextEdit>
#include <QFontMetrics>
#include <algorithm>

static QString prettyValueForHint(const QString& value);

static int clampInt(int v, int lo, int hi)
{
	return qMax(lo, qMin(hi, v));
}

static int maxLineWidthPx(const QString& text, const QFont& font, int maxLinesToScan)
{
	const QFontMetrics fm(font);
	const QStringList lines = text.split('\n');
	const int n = qMin(maxLinesToScan, lines.size());
	int best = 0;
	for (int i = 0; i < n; ++i)
		best = qMax(best, fm.horizontalAdvance(lines[i]));
	return best;
}

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
	              "}"
	              "QScrollBar:vertical { background: transparent; width: 10px; margin: 10px 4px 10px 4px; }"
	              "QScrollBar::handle:vertical { background: #CBD5E1; border-radius: 5px; min-height: 32px; }"
	              "QScrollBar::handle:vertical:hover { background: #94A3B8; }"
	              "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
	              "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
	              "QScrollBar:horizontal { background: transparent; height: 10px; margin: 4px 10px 4px 10px; }"
	              "QScrollBar::handle:horizontal { background: #CBD5E1; border-radius: 5px; min-width: 32px; }"
	              "QScrollBar::handle:horizontal:hover { background: #94A3B8; }"
	              "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
	              "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }");

	setMouseTracking(true);
	m_hoverTimer.setSingleShot(true);
	m_hoverTimer.setInterval(350);
	connect(&m_hoverTimer, &QTimer::timeout, this, [this] {
		if (!m_session || m_pendingHoverExpr.isEmpty())
			return;

		const QString expr = m_pendingHoverExpr;
		m_session->evaluateExpressionValue(expr,
			[this, expr](const QString& value, const QString& type) {
				if (expr != m_pendingHoverExpr)
					return;

				if (value.isEmpty()) {
					if (m_hoverHint)
						m_hoverHint->hide();
					m_shownHoverExpr.clear();
					return;
				}

				m_shownHoverExpr = expr;

				// Lazy-create a modern styled hover hint widget.
				if (!m_hoverHint) {
					auto* hint = new QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
					hint->setAttribute(Qt::WA_ShowWithoutActivating);
					hint->setAttribute(Qt::WA_TransparentForMouseEvents);
					hint->setObjectName("hoverHint");

						auto* title = new QLabel(hint);
						title->setObjectName("hoverHintTitle");
						title->setTextFormat(Qt::PlainText);
						title->setWordWrap(false);

						auto* meta = new QLabel(hint);
						meta->setObjectName("hoverHintMeta");
						meta->setTextFormat(Qt::PlainText);
						meta->setWordWrap(true);

					auto* sep = new QFrame(hint);
					sep->setObjectName("hoverHintSep");
					sep->setFrameShape(QFrame::HLine);
					sep->setFrameShadow(QFrame::Plain);

						auto* body = new QPlainTextEdit(hint);
						body->setObjectName("hoverHintBody");
						body->setReadOnly(true);
						body->setFrameStyle(QFrame::NoFrame);
						body->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
						body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
						body->setLineWrapMode(QPlainTextEdit::WidgetWidth);
						body->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
						body->setMaximumBlockCount(0);
						body->setContentsMargins(0, 0, 0, 0);
						body->setCursorWidth(0);
						body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

					auto* layout = new QVBoxLayout(hint);
					layout->setContentsMargins(12, 10, 12, 10);
					layout->setSpacing(6);
					layout->addWidget(title);
					layout->addWidget(meta);
					layout->addWidget(sep);
					layout->addWidget(body);

					hint->setStyleSheet(R"(
						#hoverHint {
							background: #000000;
							border: 1px solid rgba(255, 255, 255, 90);
							border-radius: 10px;
						}
						#hoverHintTitle {
							color: #ffffff;
							background: transparent;
							font-family: Menlo, Monaco, Consolas, "SF Mono", "Courier New", monospace;
							font-size: 14px;
							font-weight: 700;
						}
						#hoverHintMeta {
							color: rgba(255,255,255,210);
							background: transparent;
							font-family: Menlo, Monaco, Consolas, "SF Mono", "Courier New", monospace;
							font-size: 12px;
						}
						#hoverHintSep {
							color: rgba(255,255,255,55);
							background: rgba(255,255,255,55);
							min-height: 1px;
							max-height: 1px;
							border: none;
						}
						#hoverHintBody {
							background: transparent;
							border-radius: 0px;
							padding: 0px;
							color: #f6f6f6;
							font-family: Menlo, Monaco, Consolas, "SF Mono", "Courier New", monospace;
							font-size: 13px;
							selection-background-color: rgba(38,79,120,200);
						}
						#hoverHintBody QWidget {
							background: transparent;
							color: #f6f6f6;
						}
						#hoverHintBody QScrollBar:vertical {
							background: transparent;
							width: 10px;
							margin: 10px 4px 10px 4px;
						}
						#hoverHintBody QScrollBar::handle:vertical {
							background: #CBD5E1;
							border-radius: 5px;
							min-height: 32px;
						}
						#hoverHintBody QScrollBar::handle:vertical:hover {
							background: #94A3B8;
						}
						#hoverHintBody QScrollBar::add-line:vertical,
						#hoverHintBody QScrollBar::sub-line:vertical {
							height: 0px;
						}
						#hoverHintBody QScrollBar::add-page:vertical,
						#hoverHintBody QScrollBar::sub-page:vertical {
							background: transparent;
						}
					)");

					auto* shadow = new QGraphicsDropShadowEffect(hint);
					shadow->setBlurRadius(18);
					shadow->setOffset(0, 6);
					shadow->setColor(QColor(0, 0, 0, 160));
					hint->setGraphicsEffect(shadow);

					m_hoverHint = hint;
				}

				auto* title = m_hoverHint->findChild<QLabel*>("hoverHintTitle");
				auto* meta = m_hoverHint->findChild<QLabel*>("hoverHintMeta");
				auto* body = m_hoverHint->findChild<QPlainTextEdit*>("hoverHintBody");

				if (title)
					title->setText(expr);

					if (meta) {
						const QString shownType = type.trimmed();
						meta->setVisible(!shownType.isEmpty());
						meta->setText(shownType);
					}

					if (body) {
						body->setPlainText(prettyValueForHint(value));
						body->document()->setTextWidth(-1);
						body->moveCursor(QTextCursor::Start);
					}

					// Position near cursor, clamp to screen.
					const QPoint global = mapToGlobal(m_lastMousePos) + QPoint(14, 18);

					const int paddingX = 12 * 2;
					const int extraX = 10;

					const QString titleText = title ? title->text() : QString{};
					const QString metaText  = meta && meta->isVisible() ? meta->text() : QString{};
					const QString bodyText  = body ? body->toPlainText() : QString{};

					const int titleW = title ? QFontMetrics(title->font()).horizontalAdvance(titleText) : 0;
					const int metaW  = meta && meta->isVisible() ? QFontMetrics(meta->font()).horizontalAdvance(metaText) : 0;
					const int bodyW  = body ? maxLineWidthPx(bodyText, body->font(), 10) : 0;

					const int targetW = clampInt(
						qMax(qMax(titleW, metaW), bodyW) + paddingX + extraX,
						240,
						760
					);

					m_hoverHint->setFixedWidth(targetW);

					// Optimize height to content.
					if (body) {
						const int bodyInnerW = qMax(160, targetW - paddingX);
						body->setFixedWidth(bodyInnerW);
						body->document()->setTextWidth(bodyInnerW);
						body->document()->adjustSize();
						// Use font metrics + block count to avoid underestimating height
						// before the widget is actually shown.
						const int lineH = QFontMetrics(body->font()).height();
						const int blocks = qMax(1, body->document()->blockCount());
						const int docH = qMax(
							int(std::ceil(body->document()->size().height())),
							blocks * lineH
						);
						int maxBodyH = 260;
						if (QScreen* s = QGuiApplication::screenAt(global)) {
							const QRect avail = s->availableGeometry();
							// Leave room for title/meta/margins and keep the hint within the screen.
							maxBodyH = qMax(120, avail.height() - 180);
						}
						const int bodyH = clampInt(docH + 6, 22, maxBodyH);
						body->setFixedHeight(bodyH);
					}

					// Keep title single-line and height stable.
					if (title) {
						const QFontMetrics tfm(title->font());
						const int maxTitlePx = qMax(120, targetW - paddingX);
						title->setText(tfm.elidedText(expr, Qt::ElideRight, maxTitlePx));
					}

					m_hoverHint->adjustSize();

				QScreen* screen = QGuiApplication::screenAt(global);
				if (!screen)
					screen = QGuiApplication::primaryScreen();
				const QRect avail = screen ? screen->availableGeometry() : QRect{};

				QPoint pos = global;
				if (!avail.isNull()) {
					if (pos.x() + m_hoverHint->width() > avail.right())
						pos.setX(avail.right() - m_hoverHint->width());
					if (pos.y() + m_hoverHint->height() > avail.bottom())
						pos.setY(avail.bottom() - m_hoverHint->height());
					pos.setX(qMax(avail.left(), pos.x()));
					pos.setY(qMax(avail.top(), pos.y()));
				}

				m_hoverHint->move(pos);
				m_hoverHint->show();
			});
	});

	highlightCurrentLine();
}

static QString extractHoverExpression(const QString& lineText, int col)
{
	if (lineText.isEmpty() || col < 0 || col >= lineText.size())
		return {};

	auto isIdentChar = [](QChar c) {
		return c.isLetterOrNumber() || c == '_';
	};

	auto isSpace = [](QChar c) { return c.isSpace(); };

	// Move to nearest non-space
	int i = col;
	while (i > 0 && isSpace(lineText[i])) --i;
	while (i < lineText.size() && isSpace(lineText[i])) ++i;
	if (i < 0 || i >= lineText.size())
		return {};

	int left = i;
	int right = i;

	// Expand around an identifier
	if (isIdentChar(lineText[i])) {
		while (left > 0 && isIdentChar(lineText[left - 1]))
			--left;
		while (right + 1 < lineText.size() && isIdentChar(lineText[right + 1]))
			++right;
	} else if (lineText[i] == ']' && i > 0) {
		// Cursor on closing bracket: include bracket expression
		int depth = 0;
		int j = i;
		for (; j >= 0; --j) {
			if (lineText[j] == ']') depth++;
			else if (lineText[j] == '[') {
				--depth;
				if (depth == 0) break;
			}
		}
		if (j >= 0) {
			left = j;
			right = i;
		}
	} else {
		return {};
	}

	// Consume postfixes: .field, ->field, [index]
	auto consumeIdent = [&](int& pos) -> bool {
		int start = pos;
		while (pos < lineText.size() && isIdentChar(lineText[pos]))
			++pos;
		return pos > start;
	};

	int pos = right + 1;
	while (pos < lineText.size()) {
		if (lineText.mid(pos, 2) == "->") {
			pos += 2;
			if (!consumeIdent(pos))
				break;
			right = pos - 1;
			continue;
		}
		if (lineText[pos] == '.') {
			++pos;
			if (!consumeIdent(pos))
				break;
			right = pos - 1;
			continue;
		}
		if (lineText[pos] == '[') {
			int depth = 0;
			int j = pos;
			for (; j < lineText.size(); ++j) {
				if (lineText[j] == '[') depth++;
				else if (lineText[j] == ']') {
					--depth;
					if (depth == 0) break;
				}
			}
			if (j >= lineText.size() || lineText[j] != ']')
				break;
			right = j;
			pos = j + 1;
			continue;
		}
		break;
	}

	// Consume leading unary * and & (common for pointers)
	while (left > 0 && (lineText[left - 1] == '*' || lineText[left - 1] == '&'))
		--left;

	QString expr = lineText.mid(left, right - left + 1).trimmed();
	if (expr.isEmpty())
		return {};

	// Heuristic: avoid keywords / numbers only
	bool okNum = false;
	expr.toLongLong(&okNum, 0);
	if (okNum)
		return {};

	return expr;
}

static QString prettyValueForHint(const QString& value)
{
	QString v = value.trimmed();
	if (v.isEmpty())
		return v;

	// Compact common struct-like output.
	if (v.startsWith('{') && v.endsWith('}')) {
		QString inner = v.mid(1, v.size() - 2).trimmed();
		if (!inner.isEmpty()) {
			// Break on top-level commas to make it readable in the hint.
			QStringList parts;
			int depth = 0;
			int start = 0;
			for (int i = 0; i < inner.size(); ++i) {
				const QChar c = inner[i];
				if (c == '{' || c == '[') depth++;
				else if (c == '}' || c == ']') depth--;
				else if (c == ',' && depth == 0) {
					parts << inner.mid(start, i - start).trimmed();
					start = i + 1;
				}
			}
			parts << inner.mid(start).trimmed();

			for (QString& p : parts)
				p = "  " + p;
			v = "{\n" + parts.join(",\n") + "\n}";
		}
	}

	return v;
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

	connect(m_session, &DebuggerSession::breakpointsUpdated,
			this,
			[this] {
				if (m_lineNumberArea)
					m_lineNumberArea->update();
				viewport()->update();
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
	painter.setRenderHint(QPainter::Antialiasing, true);

	QTextBlock block = firstVisibleBlock();
	int blockNumber = block.blockNumber();
	int top = static_cast<int>(
	    blockBoundingGeometry(block).translated(contentOffset()).top());
	int bottom = top + static_cast<int>(blockBoundingRect(block).height());

	const int markerPaddingX = 3;
	const int markerColumnW = 16; // keep a stable gutter like IDEs (VS/Delphi)
	const int markerCenterX = markerPaddingX + markerColumnW / 2;
	const QString curFile = property("currentFile").toString();
	const QString curFileAbs = curFile.isEmpty() ? QString{} : QFileInfo(curFile).absoluteFilePath();

	while (block.isValid() && top <= event->rect().bottom()) {
		if (block.isVisible() && bottom >= event->rect().top()) {

			int lineIndex = blockNumber + 1;
			const int blockH = qMax(1, bottom - top);
			const int markerD = qBound(8, qMin(12, blockH - 4), 14);
			const int markerY = top + (blockH - markerD) / 2;

			const bool hasBp = m_session && !curFileAbs.isEmpty()
				? std::any_of(
					m_session->breakpoints().cbegin(),
					m_session->breakpoints().cend(),
					[&](const BreakpointInfo& bp) {
						if (bp.line != lineIndex)
							return false;
						if (bp.file.isEmpty())
							return false;
						return QFileInfo(bp.file).absoluteFilePath() == curFileAbs;
					}
				)
				: false;

			if (lineIndex == m_currentPC) {
				// Visual Studio-like execution arrow.
				const qreal cx = markerCenterX;
				const qreal cy = markerY + markerD / 2.0;
				const qreal h = markerD * 0.9;
				const qreal w = markerD * 1.15;

				QPainterPath arrow;
				arrow.moveTo(cx - w * 0.55, cy - h * 0.5);
				arrow.lineTo(cx + w * 0.15, cy - h * 0.5);
				arrow.lineTo(cx + w * 0.55, cy);
				arrow.lineTo(cx + w * 0.15, cy + h * 0.5);
				arrow.lineTo(cx - w * 0.55, cy + h * 0.5);
				arrow.closeSubpath();

				QLinearGradient g(QPointF(0, markerY), QPointF(0, markerY + markerD));
				g.setColorAt(0.0, QColor("#fff3a0"));
				g.setColorAt(1.0, QColor("#ffc400"));

				painter.setPen(QPen(QColor("#9a6b00"), 1.0));
				painter.setBrush(g);
				painter.drawPath(arrow);
			}

			if (hasBp) {
				// Breakpoint dot (VS/Delphi-like): glossy red with dark border.
				const QRectF r(markerCenterX - markerD / 2.0, markerY, markerD, markerD);
				QRadialGradient grad(r.center() - QPointF(markerD * 0.2, markerD * 0.2),
				                     markerD * 0.75);
				grad.setColorAt(0.0, QColor("#ff8a8a"));
				grad.setColorAt(0.55, QColor("#e53935"));
				grad.setColorAt(1.0, QColor("#8b1d1d"));

				painter.setPen(QPen(QColor("#3a0f0f"), 1.0));
				painter.setBrush(grad);
				painter.drawEllipse(r);

				// Subtle highlight.
				painter.setPen(Qt::NoPen);
				painter.setBrush(QColor(255, 255, 255, 60));
				painter.drawEllipse(r.adjusted(markerD * 0.18, markerD * 0.16,
				                               -markerD * 0.42, -markerD * 0.52));
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

void SourceEditor::mouseMoveEvent(QMouseEvent *event)
{
	QPlainTextEdit::mouseMoveEvent(event);

	m_lastMousePos = event->pos();

	if (!m_session) {
		m_pendingHoverExpr.clear();
		if (m_hoverHint)
			m_hoverHint->hide();
		return;
	}

	// Only show value hints when the target is stopped (avoid spamming while running)
	// We don't have an explicit "target running" flag, but tooltips are most useful
	// after a stop event anyway.

	QTextCursor c = cursorForPosition(event->pos());
	const QString lineText = c.block().text();
	const int col = c.position() - c.block().position();

	const QString expr = extractHoverExpression(lineText, col);
	if (expr.isEmpty()) {
		m_pendingHoverExpr.clear();
		if (!m_shownHoverExpr.isEmpty()) {
			if (m_hoverHint)
				m_hoverHint->hide();
			m_shownHoverExpr.clear();
		}
		m_hoverTimer.stop();
		return;
	}

	if (expr == m_pendingHoverExpr)
		return;

	m_pendingHoverExpr = expr;
	m_hoverTimer.start();
}

void SourceEditor::leaveEvent(QEvent *event)
{
	QPlainTextEdit::leaveEvent(event);
	m_hoverTimer.stop();
	m_pendingHoverExpr.clear();
	m_shownHoverExpr.clear();
	if (m_hoverHint)
		m_hoverHint->hide();
}

void SourceEditor::currentLocation(QString &file, int &line) const {
	file = this->property("currentFile")
	           .toString();
	line = textCursor().blockNumber() + 1;
}

void SourceEditor::setBreakpointsUpdated(const QSet<int>& lines)
{
	Q_UNUSED(lines);
	if (m_hoverHint)
		m_hoverHint->hide();
	viewport()->update();
}
