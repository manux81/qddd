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

#include "DataDisplayView.h"
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>

DataDisplayView::DataDisplayView(QWidget *parent)
    : QGraphicsView(parent), m_scene(new QGraphicsScene(this)) {
	setScene(m_scene);
	addDummyGraph();
}

void DataDisplayView::clearDisplay() { m_scene->clear(); }

void DataDisplayView::addDummyGraph() {
	clearDisplay();

	auto *node1 = m_scene->addEllipse(-30, -30, 60, 60);
	auto *text1 = m_scene->addText(QStringLiteral("obj1"));
	text1->setPos(-20, -15);

	auto *node2 = m_scene->addEllipse(100, -30, 60, 60);
	auto *text2 = m_scene->addText(QStringLiteral("obj2"));
	text2->setPos(110, -15);

	m_scene->addLine(30, 0, 100, 0);
}
