#pragma once
#include "DebugSession.h"
#include <QTreeWidget>
class ThreadsView : public QTreeWidget {
public:
    explicit ThreadsView(DebuggerSession* session, QWidget* parent = nullptr) : QTreeWidget(parent) {
        setHeaderLabels({tr("Thread"), tr("Name"), tr("State")});
        connect(session, &DebuggerSession::threadsUpdated, this, [this, session] {
            clear();
            for (const auto& thread : session->threads()) {
                auto* item = new QTreeWidgetItem(this, {thread.id, thread.name, thread.state});
                if (thread.id == session->selectedThread()) setCurrentItem(item);
            }
        });
        connect(this, &QTreeWidget::itemActivated, this, [session](QTreeWidgetItem* item, int) { session->selectThread(item->text(0)); });
    }
};
