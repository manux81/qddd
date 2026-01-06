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

#include "QDDDSplash.h"
#include <QGuiApplication>
#include <QPainter>
#include <QRandomGenerator>
#include <QScreen>

QDDDSplash::QDDDSplash(const QString &imagePath, QWidget *parent)
    : QWidget(parent), m_pix(imagePath) {
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
	setAttribute(Qt::WA_TranslucentBackground);
	resize(450, 260);

	auto screen = QGuiApplication::primaryScreen()->availableGeometry();
	move(screen.center().x() - width() / 2, screen.center().y() - height() / 2);

	m_basePos = pos();

	connect(&m_trembleTimer, &QTimer::timeout, this, [=]() {
		int dx = QRandomGenerator::global()->bounded(-1, 2);
		int dy = QRandomGenerator::global()->bounded(-1, 2);
		move(m_basePos.x() + dx, m_basePos.y() + dy);
	});
}

void QDDDSplash::startTremble() { m_trembleTimer.start(45); }

void QDDDSplash::stopTremble() {
	m_trembleTimer.stop();
	move(m_basePos);
}

void QDDDSplash::paintEvent(QPaintEvent *) {
	QPainter p(this);
	p.setRenderHint(QPainter::SmoothPixmapTransform);

	QSize scaled = m_pix.size();
	scaled.scale(size(), Qt::KeepAspectRatio);

	QPoint topLeft((width() - scaled.width()) / 2,
	               (height() - scaled.height()) / 2);

	p.drawPixmap(topLeft, m_pix.scaled(scaled, Qt::KeepAspectRatio,
	                                   Qt::SmoothTransformation));
}
