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

#include "VariablesView.h"
#include "HardwareDebugSession.h"

#include <QFontDatabase>
#include <QHeaderView>
#include <QPainter>
#include <QPalette>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QSet>
#include <QHash>
#include <QMenu>

static constexpr int ChangedRole = Qt::UserRole + 1;
static constexpr int WatchRole = Qt::UserRole + 2;

namespace {

constexpr int VariablePathRole = Qt::UserRole + 3;

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
			name = s.left(i).trimmed();
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

VarVisualType visualType(const DebugVariable *n) {
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
	default:                       return QColor(220, 220, 220);
	}
}

/* ============================================================
 *  ICON RENDERING
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
	default:
		return coloredIcon(":/icons/resources/icons/symbol-variable.svg", c);
	}
}

QString itemPath(QStandardItem *item) {
	QStringList parts;
	while (item) {
		parts.prepend(item->text());
		item = item->parent();
	}
	QString path;
	for (const QString& part : parts) {
		if (path.isEmpty() || part.startsWith('['))
			path += part;
		else
			path += "." + part;
	}
	return path;
}

QString childPath(const QString& parentPath, const QString& childName) {
	return childName.startsWith('[')
		? parentPath + childName
		: parentPath + "." + childName;
}

} // namespace

/* ============================================================
 *  VALUE DELEGATE (highlight stile VS)
 * ============================================================ */

class ValueDelegate : public QStyledItemDelegate {
  public:
	explicit ValueDelegate(QObject *parent = nullptr);

	void paint(QPainter *p, const QStyleOptionViewItem &opt,
	           const QModelIndex &idx) const override {
		QStyleOptionViewItem o(opt);
		initStyleOption(&o, idx);

		o.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);

		const bool changed = idx.data(ChangedRole).toBool();
		if (changed) {
			p->save();
			QColor bg(0, 60, 20);
			p->fillRect(o.rect, bg);
			p->restore();
		}

		QStyledItemDelegate::paint(p, o, idx);
	}
};

ValueDelegate::ValueDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

/* ============================================================
 *  VARIABLES VIEW
 * ============================================================ */

VariablesView::VariablesView(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QStandardItemModel(this))
{
    m_model->setHorizontalHeaderLabels({tr("Name"), tr("Value")/*, tr("Type")*/});
	setModel(m_model);

	header()->setStretchLastSection(true);
	header()->setHighlightSections(false);

	setAlternatingRowColors(true);
	QPalette pal = palette();
	pal.setColor(QPalette::Base, QColor(30, 30, 30));          // riga pari
	pal.setColor(QPalette::AlternateBase, QColor(36, 36, 36)); // riga dispari
	setPalette(pal);

	setUniformRowHeights(true);
	setRootIsDecorated(true);
	setIndentation(14);
	setIconSize(QSize(20, 20));
	setStyleSheet("QScrollBar:vertical { background: transparent; width: 10px; margin: 10px 4px 10px 4px; }"
	              "QScrollBar::handle:vertical { background: #CBD5E1; border-radius: 5px; min-height: 32px; }"
	              "QScrollBar::handle:vertical:hover { background: #94A3B8; }"
	              "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
	              "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
	              "QScrollBar:horizontal { background: transparent; height: 10px; margin: 4px 10px 4px 10px; }"
	              "QScrollBar::handle:horizontal { background: #CBD5E1; border-radius: 5px; min-width: 32px; }"
	              "QScrollBar::handle:horizontal:hover { background: #94A3B8; }"
	              "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
	              "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }");

	setItemDelegateForColumn(1, new ValueDelegate(this));
	connect(m_model, &QStandardItemModel::itemChanged,
	        this, &VariablesView::commitValue);
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		const QModelIndex index = indexAt(pos);
		if (!index.isValid())
			return;
		const QModelIndex nameIndex = index.siblingAtColumn(0);
		if (!nameIndex.data(WatchRole).toBool() || !m_session)
			return;
		QMenu menu(this);
		QAction* remove = menu.addAction(tr("Remove watch"));
		if (menu.exec(viewport()->mapToGlobal(pos)) == remove)
			m_session->removeWatchExpression(nameIndex.data(Qt::DisplayRole).toString());
	});
}

void VariablesView::setSession(DebuggerSession *session) {
	if (m_session == session)
		return;

	if (m_session) {
		disconnect(m_session, nullptr, this, nullptr);
		m_session = nullptr;
	}
	m_session = session;

	if (session) {
		connect(m_session, &DebuggerSession::variablesUpdated,
		        this, &VariablesView::refresh);
	}
}

void VariablesView::setHardwareSession(HardwareDebugSession* session)
{
	m_hardwareSession = session;
	if (session)
		connect(session, &HardwareDebugSession::mdbVariablesChanged,
		        this, &VariablesView::refresh, Qt::UniqueConnection);
}

void VariablesView::clearVariables() {
	m_model->clear();
    m_model->setHorizontalHeaderLabels({tr("Name"), tr("Value")/*, tr("Type")*/});
}

void VariablesView::refresh()
{
    if (!m_session)
        return;

    QSet<QString> expanded;

    std::function<void(QStandardItem*)> save = [&](QStandardItem* it) {
        if (!it)
            return;

        if (isExpanded(m_model->indexFromItem(it)))
            expanded.insert(itemPath(it));

        for (int i = 0; i < it->rowCount(); ++i)
            save(it->child(i));
    };

    for (int i = 0; i < m_model->rowCount(); ++i)
        save(m_model->item(i));


	m_refreshing = true;
	clearVariables();

	for (const auto& n : m_session->variables())
		addNode(nullptr, n.get());
	m_refreshing = false;



    std::function<void(QStandardItem*)> restore = [&](QStandardItem* it) {
        if (!it)
            return;

        if (expanded.contains(itemPath(it)))
            expand(m_model->indexFromItem(it));

        for (int i = 0; i < it->rowCount(); ++i)
            restore(it->child(i));
    };

    for (int i = 0; i < m_model->rowCount(); ++i)
        restore(m_model->item(i));
}

void VariablesView::addNode(QStandardItem *parent, DebugVariable *node)
{
	if (!node)
		return;

	auto *nameItem  = new QStandardItem(node->name);
	auto *valueItem = new QStandardItem(node->value);
	nameItem->setData(node->isWatch, WatchRole);

	const VarVisualType vt = visualType(node);
	nameItem->setIcon(iconForType(vt));
	nameItem->setEditable(false);

	const QString path = parent
		? childPath(itemPath(parent), node->name)
		: node->name;

	const bool isLeafValue = node->children.empty();
	valueItem->setEditable(isLeafValue);
	if (isLeafValue)
		valueItem->setData(path, VariablePathRole);

	const bool nodeChanged =
		isLeafValue &&
		m_session &&
		m_session->changedPaths().contains(path);

	valueItem->setData(nodeChanged, ChangedRole);

	QList<QStandardItem*> row{ nameItem, valueItem };
	if (parent)
		parent->appendRow(row);
	else
		m_model->appendRow(row);

	// ------------------------------------------------------------
	// REAL debugger children
	// ------------------------------------------------------------
	if (!node->children.empty()) {
		// Keep pointer address visible while still allowing expansion.
		if (!node->isPointer) {
			valueItem->setText("[ ]");
			valueItem->setForeground(QColor(160, 160, 160));
		}
		for (const auto &c : node->children)
			if (c) addNode(nameItem, c.get());
		return;
	}

	// ------------------------------------------------------------
	// STRUCT PARSING with FIELD-LEVEL DIFF
	// ------------------------------------------------------------
	const QString cur = node->value.trimmed();
	if (!cur.startsWith('{') || !cur.endsWith('}'))
		return;

	valueItem->setEditable(false);
	valueItem->setData(QVariant(), VariablePathRole);
	valueItem->setText("[ ]");
	valueItem->setForeground(QColor(160, 160, 160));

	// ---- current fields
	QHash<QString, QString> curFields;

	const QString inner = cur.mid(1, cur.size() - 2);
	const auto parts = splitTopLevel(inner);

	for (const QString &p : parts) {
	    QString n, v;
	    if (splitNameValue(p, n, v))
	        curFields[n] = v;
	}


	// ---- previous fields (if any)
	QHash<QString, QString> prevFields;
	if (m_session) {
		const auto &hist = m_session->executionHistory();
		if (hist.size() >= 2) {
			const auto &prev = hist[hist.size() - 2];
			if (prev.variableValues.contains(path)) {
				const QString prevVal = prev.variableValues[path].trimmed();
				if (prevVal.startsWith('{') && prevVal.endsWith('}')) {
					for (const QString &p : splitTopLevel(prevVal.mid(1, prevVal.size() - 2))) {
						QString n, v;
						if (splitNameValue(p, n, v))
							prevFields[n] = v;
					}
				}
			}
		}
	}

	// ---- create children, highlight ONLY changed field
	for (auto it = curFields.begin(); it != curFields.end(); ++it) {
		auto *cn = new QStandardItem(it.key());
		auto *cv = new QStandardItem(it.value());

		cn->setIcon(iconForType(VarVisualType::Scalar));
		cn->setEditable(false);
		cv->setData(childPath(path, it.key()), VariablePathRole);

		const bool fieldChanged =
			prevFields.contains(it.key()) &&
			prevFields[it.key()] != it.value();

		cv->setData(fieldChanged, ChangedRole);

		nameItem->appendRow({ cn, cv });
	}
}

void VariablesView::commitValue(QStandardItem *item)
{
	if (m_refreshing || !m_session || !item || item->column() != 1)
		return;

	const QString path = item->data(VariablePathRole).toString();
	if (path.isEmpty())
		return;

	if (m_hardwareSession && m_hardwareSession->isActive() && m_hardwareSession->usesMdb())
		m_hardwareSession->setMdbVariable(path, item->text());
	else
		m_session->setVariable(path, item->text());
}
