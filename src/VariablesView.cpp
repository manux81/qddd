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

#include "VariablesView.h"

#include <QFontDatabase>
#include <QHeaderView>
#include <QPainter>
#include <QPalette>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QSet>
#include <QHash>

namespace {

/* ============================================================
 *  PARSER HELPERS
 * ============================================================ */

QStringList splitTopLevel(const QString &s) {
	QStringList result;
	QString current;
	int depth = 0;

	for (QChar c : s) {
		if (c == '{')
			++depth;
		else if (c == '}')
			--depth;

		if (c == ',' && depth == 0) {
			result << current.trimmed();
			current.clear();
		} else {
			current.append(c);
		}
	}

	if (!current.trimmed().isEmpty())
		result << current.trimmed();

	return result;
}

bool splitNameValue(const QString &s, QString &name, QString &value) {
	int depth = 0;

	for (int i = 0; i < s.size(); ++i) {
		QChar c = s[i];

		if (c == '{')
			++depth;
		else if (c == '}')
			--depth;

		if (c == '=' && depth == 0) {
			name  = s.left(i).trimmed();
			value = s.mid(i + 1).trimmed();
			return true;
		}
	}
	return false;
}

/* ============================================================
 *  VISUAL SEMANTICS
 * ============================================================ */

enum class VarVisualType {
	Scalar,
	Pointer,
	Struct,
	Object,
	Container,
	Internal
};

VarVisualType visualType(const VarNode *n) {
	if (n->name.startsWith("d_"))
		return VarVisualType::Internal;
	if (n->value.startsWith("0x"))
		return VarVisualType::Pointer;
	if (n->value.startsWith('{') && n->value.endsWith('}'))
		return VarVisualType::Struct;
	if (n->type.contains("QObject"))
		return VarVisualType::Object;
	if (n->type.contains("vector") || n->type.contains("array"))
		return VarVisualType::Container;
	return VarVisualType::Scalar;
}

QColor colorForType(VarVisualType type) {
	switch (type) {
	case VarVisualType::Pointer:   return QColor(97, 175, 239);
	case VarVisualType::Struct:    return QColor(152, 195, 121);
	case VarVisualType::Object:    return QColor(229, 192, 123);
	case VarVisualType::Container: return QColor(198, 120, 221);
	case VarVisualType::Internal:  return QColor(130, 130, 130);
	case VarVisualType::Scalar:
	default:                       return QColor(220, 220, 220);
	}
}

/* ============================================================
 *  ICON RENDERING (SVG → colored pixmap)
 * ============================================================ */

QIcon coloredIcon(const QString &path, const QColor &color, int size = 16) {
	QPixmap pm(size, size);
	pm.fill(Qt::transparent);

	QIcon base(path);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing, true);

	base.paint(&p, QRect(0, 0, size, size));
	p.setCompositionMode(QPainter::CompositionMode_SourceIn);
	p.fillRect(pm.rect(), color);

	return QIcon(pm);
}

QIcon iconForType(VarVisualType type) {
	const QColor c = colorForType(type);
	switch (type) {
	case VarVisualType::Pointer:
		return coloredIcon(":/icons/resources/icons/symbol-field.svg", c);
	case VarVisualType::Struct:
		return coloredIcon(":/icons/resources/icons/symbol-structure.svg", c);
	case VarVisualType::Object:
		return coloredIcon(":/icons/resources/icons/symbol-class.svg", c);
	case VarVisualType::Container:
		return coloredIcon(":/icons/resources/icons/symbol-array.svg", c);
	case VarVisualType::Internal:
		return coloredIcon(":/icons/resources/icons/symbol-key.svg", c);
	case VarVisualType::Scalar:
	default:
		return coloredIcon(":/icons/resources/icons/symbol-variable.svg", c);
	}
}

/* ============================================================
 *  PATH UTILITY (per expand + diff)
 * ============================================================ */

QString itemPath(QStandardItem *item) {
	QStringList parts;
	while (item) {
		parts.prepend(item->text());
		item = item->parent();
	}
	return parts.join(".");
}

} // namespace

/* ============================================================
 *  VALUE DELEGATE
 * ============================================================ */

class ValueDelegate : public QStyledItemDelegate {
public:
	explicit ValueDelegate(QObject *parent = nullptr)
	    : QStyledItemDelegate(parent) {}

	void paint(QPainter *p, const QStyleOptionViewItem &opt,
	           const QModelIndex &idx) const override {
		QStyleOptionViewItem o(opt);
		o.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		QStyledItemDelegate::paint(p, o, idx);
	}
};

/* ============================================================
 *  VARIABLES VIEW
 * ============================================================ */

VariablesView::VariablesView(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QStandardItemModel(this)) {

	m_model->setHorizontalHeaderLabels({tr("Name"), tr("Value"), tr("Type")});
	setModel(m_model);

	header()->setStretchLastSection(true);
	header()->setHighlightSections(false);

	setAlternatingRowColors(true);
	setUniformRowHeights(true);
	setRootIsDecorated(true);
	setIndentation(14);
	setIconSize(QSize(20, 20));

	setItemDelegateForColumn(1, new ValueDelegate(this));
}

void VariablesView::setSession(DebugSession *session) {
	if (m_session == session)
		return;

	if (m_session)
		disconnect(m_session, nullptr, this, nullptr);

	m_session = session;

	if (m_session)
		connect(m_session, &DebugSession::sessionUpdated,
		        this, &VariablesView::refresh);
}

void VariablesView::clearVariables() {
	m_model->clear();
	m_model->setHorizontalHeaderLabels({tr("Name"), tr("Value"), tr("Type")});
}

void VariablesView::refresh() {
	if (!m_session)
		return;

	/* -------- save expanded state -------- */
	QSet<QString> expanded;
	std::function<void(QStandardItem*)> save =
	    [&](QStandardItem *it) {
		    if (!it) return;
		    if (isExpanded(m_model->indexFromItem(it)))
			    expanded.insert(itemPath(it));
		    for (int i = 0; i < it->rowCount(); ++i)
			    save(it->child(i));
	    };

	for (int i = 0; i < m_model->rowCount(); ++i)
		save(m_model->item(i));

	/* -------- rebuild -------- */
	clearVariables();

	for (const VariableInfo &v : m_session->variables()) {
		auto *n = new VarNode;
		n->name = v.name;
		n->value = v.value;
		n->type = v.type;
		addNode(nullptr, n);
	}

	/* -------- restore expanded -------- */
	std::function<void(QStandardItem*)> restore =
	    [&](QStandardItem *it) {
		    if (!it) return;
		    if (expanded.contains(itemPath(it)))
			    expand(m_model->indexFromItem(it));
		    for (int i = 0; i < it->rowCount(); ++i)
			    restore(it->child(i));
	    };

	for (int i = 0; i < m_model->rowCount(); ++i)
		restore(m_model->item(i));

	/* -------- update value cache -------- */
	m_previousValues.clear();
	std::function<void(QStandardItem*)> collect =
	    [&](QStandardItem *it) {
		    if (!it) return;
		    QString path = itemPath(it);
		    QStandardItem *valueItem = nullptr;

if (it->parent())
	valueItem = it->parent()->child(it->row(), 1);
else
	valueItem = m_model->item(it->row(), 1);

if (!valueItem)
	return;

QString val = valueItem->text();

		    m_previousValues[path] = val;
		    for (int i = 0; i < it->rowCount(); ++i)
			    collect(it->child(i));
	    };

	for (int i = 0; i < m_model->rowCount(); ++i)
		collect(m_model->item(i));
}

void VariablesView::addNode(QStandardItem *parent, VarNode *node) {
	if (!node)
		return;

	auto *nameItem  = new QStandardItem(node->name);
	auto *valueItem = new QStandardItem(node->value);
	auto *typeItem  = new QStandardItem(node->type);

	const VarVisualType vt = visualType(node);
	nameItem->setIcon(iconForType(vt));

	const QString path = parent
	    ? itemPath(parent) + "." + node->name
	    : node->name;

	const QString oldVal = m_previousValues.value(path);
	if (!oldVal.isEmpty() && oldVal != node->value) {
		valueItem->setBackground(QColor(60, 60, 60));
		valueItem->setForeground(QColor(255, 180, 80));
	}

	QList<QStandardItem *> row{ nameItem, valueItem, typeItem };
	if (parent)
		parent->appendRow(row);
	else
		m_model->appendRow(row);

	const QString v = node->value.trimmed();
	if (node->children.isEmpty() && v.startsWith('{') && v.endsWith('}')) {
		const QString inside = v.mid(1, v.length() - 2);
		for (const QString &f : splitTopLevel(inside)) {
			QString n, val;
			if (splitNameValue(f, n, val)) {
				auto *c = new VarNode;
				c->name = n;
				c->value = val;
				addNode(nameItem, c);
			}
		}
		valueItem->setText("{...}");
		valueItem->setForeground(QColor(160, 160, 160));
	}
}
