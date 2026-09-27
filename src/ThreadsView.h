#pragma once
#include "DebugSession.h"
#include <QTreeWidget>
class ThreadsView : public QTreeWidget {
public:
    explicit ThreadsView(DebuggerSession* session, QWidget* parent = nullptr) : QTreeWidget(parent), m_session(session) {
        setHeaderLabels({tr("Thread"), tr("Name"), tr("State")});
        connect(session, &DebuggerSession::threadsUpdated, this, &ThreadsView::refresh);
        connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
            if (m_historic || !m_session)
                return;
            m_session->selectThread(item->text(0));
        });
        refresh();
    }
    // Historic inspection: snapshots carry no thread list, so show an
    // explicit notice instead of the stale live list.
    void setHistoric(bool historic)
    {
        if (m_historic == historic)
            return;
        m_historic = historic;
        refresh();
    }
    void refresh()
    {
        if (!m_session)
            return;
        clear();
        if (m_historic) {
            auto* notice = new QTreeWidgetItem(
                this, {tr("Threads — not captured for this history point")});
            notice->setDisabled(true);
            return;
        }
        for (const auto& thread : m_session->threads()) {
            auto* item = new QTreeWidgetItem(this, {thread.id, thread.name, thread.state});
            if (thread.id == m_session->selectedThread()) setCurrentItem(item);
        }
    }
private:
    DebuggerSession* m_session = nullptr;
    bool m_historic = false;
};
