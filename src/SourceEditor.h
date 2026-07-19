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

#pragma once

#include <QPlainTextEdit>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <QPointer>

#include "DebugSession.h"

class LineNumberArea;

class SourceEditor : public QPlainTextEdit {
	Q_OBJECT
  public:
	explicit SourceEditor(QWidget *parent = nullptr);
	void setSession(DebuggerSession *session);
	void showLocation(const QString &file, int line);
	int lineNumberAreaWidth();
	void lineNumberAreaPaintEvent(QPaintEvent *event);
	void lineNumberAreaMousePressEvent(QMouseEvent *event);
	void setCurrentPC(int line);

	void currentLocation(QString &file, int &line) const;
	void setBreakpointLines(const QSet<int>& lines);

  public slots:
	void setBreakpointsUpdated(const QSet<int>& lines);

  signals:
	void runUntilCursorRequested(const QString &file, int line);
    void toggleBreakpointRequested(const QString& file, int line);
    void runUntilRequested(const QString& file, int line);


  protected:
	void resizeEvent(QResizeEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;

  private slots:
	void updateLineNumberAreaWidth(int newBlockCount);
	void updateLineNumberArea(const QRect &rect, int dy);
	void highlightCurrentLine();

  private:
	LineNumberArea *m_lineNumberArea;
	int m_currentPC = -1;
	DebuggerSession *m_session = nullptr;

	QTimer m_hoverTimer;
	QPoint m_lastMousePos;
	QString m_pendingHoverExpr;
	QString m_shownHoverExpr;
	QPointer<QWidget> m_hoverHint;
};

class LineNumberArea : public QWidget {
  public:
	explicit LineNumberArea(SourceEditor *editor);

	QSize sizeHint() const override;

  protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;

  private:
	SourceEditor *m_editor;
};
