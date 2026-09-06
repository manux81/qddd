#pragma once
#include "DebugSession.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QPointer>
class SemanticValueView : public QWidget {
public:
    SemanticValueView(DebuggerSession* session, const QString& expression, QWidget* parent=nullptr) : QWidget(parent) {
        auto* layout=new QVBoxLayout(this);
        auto* summary=new QLabel(tr("Loading %1…").arg(expression),this); summary->setWordWrap(true); layout->addWidget(summary);
        auto* children=new QTableWidget(this); children->setColumnCount(3);
        children->setHorizontalHeaderLabels({tr("Member / key"),tr("Value"),tr("Type")});
        children->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(children);
        QPointer<SemanticValueView> guard(this);
        session->inspectValue(expression,[guard,summary,children](SemanticValue value) {
            if (!guard) return;
            summary->setText(value.error.isEmpty() ? value.displayName+"\n"+value.summary+
                (value.truncated ? tr("\nShowing the first 200 debugger children.") : QString()) : value.error);
            children->setRowCount(value.children.size());
            for(int i=0;i<value.children.size();++i) {
                const auto& child=value.children[i];
                children->setItem(i,0,new QTableWidgetItem(child.name));
                children->setItem(i,1,new QTableWidgetItem(child.value));
                children->setItem(i,2,new QTableWidgetItem(child.type));
            }
            children->resizeColumnsToContents();
        });
    }
};
