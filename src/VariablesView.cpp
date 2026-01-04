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
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QPalette>
#include <QPainter>


namespace {

/*
 * ============================================================
 *  PARSER HELPERS (robusto per struct annidate LLDB)
 * ============================================================
 */

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
		const QChar c = s[i];

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

/*
 * ============================================================
 *  ICON SEMANTICS (VS Code Codicons reali)
 * ============================================================
 */

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

// Colori semantici stile Visual Studio
static QColor colorForType(VarVisualType type) {
	switch (type) {

	case VarVisualType::Pointer:
		// indirizzi / reference → azzurro
		return QColor(97, 175, 239);   // VS blue

	case VarVisualType::Struct:
		// aggregate / struct → verde tenue
		return QColor(152, 195, 121);  // VS green

	case VarVisualType::Object:
		// classi / QObject → giallo caldo
		return QColor(229, 192, 123);  // VS yellow

	case VarVisualType::Container:
		// array / vector → viola tenue
		return QColor(198, 120, 221);  // VS purple

	case VarVisualType::Internal:
		// implementation detail → grigio spento
		return QColor(130, 130, 130);

	case VarVisualType::Scalar:
	default:
		// variabili normali → testo standard
		return QColor(220, 220, 220);
	}
}

static QIcon coloredIcon(const QString &path, const QColor &color, int size = 16)
{
	QIcon baseIcon(path);
	QPixmap pm(size, size);
	pm.fill(Qt::transparent);

	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing, true);

	// Render SVG in pixmap
	baseIcon.paint(&p, QRect(0, 0, size, size));

	// Applica colore (tinta)
	p.setCompositionMode(QPainter::CompositionMode_SourceIn);
	p.fillRect(pm.rect(), color);

	p.end();
	return QIcon(pm);
}

static QIcon iconForType(VarVisualType type)
{
	const QColor color = colorForType(type);

	switch (type) {
	case VarVisualType::Pointer:
		return coloredIcon(":/icons/resources/icons/symbol-field.svg", color);
	case VarVisualType::Struct:
		return coloredIcon(":/icons/resources/icons/symbol-structure.svg", color);
	case VarVisualType::Object:
		return coloredIcon(":/icons/resources/icons/symbol-class.svg", color);
	case VarVisualType::Container:
		return coloredIcon(":/icons/resources/icons/symbol-array.svg", color);
	case VarVisualType::Internal:
		return coloredIcon(":/icons/resources/icons/symbol-key.svg", color);
	case VarVisualType::Scalar:
	default:
		return coloredIcon(":/icons/resources/icons/symbol-variable.svg", color);
	}
}

} // namespace

/*
 * ============================================================
 *  VALUE DELEGATE (monospace stile debugger)
 * ============================================================
 */

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

/*
 * ============================================================
 *  VARIABLES VIEW
 * ============================================================
 */

VariablesView::VariablesView(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QStandardItemModel(this)) {

	m_model->setHorizontalHeaderLabels({tr("Name"), tr("Value"), tr("Type")});
	setModel(m_model);

	header()->setStretchLastSection(true);
	header()->setHighlightSections(false);

	setAlternatingRowColors(true);
	setRootIsDecorated(true);
	setUniformRowHeights(true);
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

	if (m_session) {
		connect(m_session, &DebugSession::sessionUpdated,
		        this, &VariablesView::refresh);
	}
}

void VariablesView::clearVariables() {
	m_model->clear();
	m_model->setHorizontalHeaderLabels({tr("Name"), tr("Value"), tr("Type")});
}

void VariablesView::refresh() {
	if (!m_session)
		return;

	clearVariables();

	for (const VariableInfo &v : m_session->variables()) {
		auto *node = new VarNode;
		node->name = v.name;
		node->value = v.value;
		node->type = v.type;
		node->hasChildren = false;
		addNode(nullptr, node);
	}
}

void VariablesView::addNode(QStandardItem *parent, VarNode *node) {
	if (!node)
		return;

	auto *nameItem  = new QStandardItem(node->name);
	auto *valueItem = new QStandardItem(node->value);
	auto *typeItem  = new QStandardItem(node->type);

	// ---------- ICON + SEMANTIC STYLING ----------
	const VarVisualType vt = visualType(node);
	nameItem->setIcon(iconForType(vt));

	// icone leggermente più chiare del testo (VS-style)
	nameItem->setForeground(palette().color(QPalette::Text).lighter(130));


	if (vt == VarVisualType::Internal) {
		QFont f = nameItem->font();
		f.setItalic(true);
		nameItem->setFont(f);
		nameItem->setForeground(QColor(150, 150, 150));
	}

	if (node->value.startsWith("0x")) {
		valueItem->setForeground(QColor(140, 180, 220));
		valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	}

	// ---------- APPEND ROW ----------
	QList<QStandardItem *> row{ nameItem, valueItem, typeItem };
	if (parent)
		parent->appendRow(row);
	else
		m_model->appendRow(row);

	// ---------- FALLBACK PARSER ----------
	const QString value = node->value.trimmed();
	const bool shouldParseStruct =
	    node->children.isEmpty() &&
	    value.startsWith('{') &&
	    value.endsWith('}');

	if (shouldParseStruct) {
		const QString inside = value.mid(1, value.length() - 2);
		const QStringList fields = splitTopLevel(inside);

		for (const QString &field : fields) {
			QString name, val;
			if (!splitNameValue(field, name, val))
				continue;

			auto *child = new VarNode;
			child->name = name;
			child->value = val;
			child->type.clear();
			child->hasChildren =
			    val.startsWith('{') && val.endsWith('}');
			node->children.append(child);
		}

		if (!node->children.isEmpty()) {
			valueItem->setText("...");
			valueItem->setForeground(QColor(160, 160, 160));
		}
	}

	for (VarNode *child : node->children)
		addNode(nameItem, child);
}
