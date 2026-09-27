#include <QTimer>
#pragma once
#include "DebugSession.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QPointer>
#include <QFontDatabase>

class MemoryView : public QWidget {
public:
    explicit MemoryView(DebuggerSession* session, QWidget* parent = nullptr) : QWidget(parent), m_session(session) {
        auto* layout = new QVBoxLayout(this);
        auto* row = new QHBoxLayout;
        m_address = new QLineEdit(this); m_address->setPlaceholderText(tr("Address or pointer expression"));
        m_count = new QSpinBox(this); m_count->setRange(1,65536); m_count->setValue(256); m_count->setSuffix(tr(" bytes"));
        auto* refresh = new QPushButton(tr("Read / refresh"), this);
        auto* follow = new QPushButton(tr("Follow pointer at address"), this);
        row->addWidget(m_address); row->addWidget(m_count); row->addWidget(refresh); row->addWidget(follow); layout->addLayout(row);
        m_output = new QPlainTextEdit(this); m_output->setReadOnly(true);
        m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); layout->addWidget(m_output);
        connect(refresh, &QPushButton::clicked, this, [this] { read(); });
        connect(m_address, &QLineEdit::returnPressed, this, [this] { read(); });
        connect(follow, &QPushButton::clicked, this, [this] {
            m_address->setText(QString("*(void**)(%1)").arg(m_address->text())); read();
        });
        connect(session, &DebuggerSession::targetExited, this, [this](int) { ++m_generation; m_output->setPlainText(tr("Target exited.")); });
        m_readButton = refresh;
        m_followButton = follow;
    }
    void openAddress(const QString& address) { m_address->setText(address); read(); }
    // Historic inspection: memory reads would hit the live target, so the
    // actions are disabled with an explicit notice instead.
    void setHistoric(bool historic)
    {
        if (m_historic == historic)
            return;
        m_historic = historic;
        if (m_readButton)
            m_readButton->setEnabled(!historic);
        if (m_followButton)
            m_followButton->setEnabled(!historic);
        if (historic) {
            ++m_generation; // invalidate in-flight live reads
            m_output->setPlainText(
                tr("Memory — not captured for this history point.\n"
                   "Reads would hit the live target, so they are disabled."));
        }
    }
private:
    void read() {
        if (m_historic)
            return;
        const auto generation = ++m_generation;
        m_output->setPlainText(tr("Reading memory…"));
        QPointer<MemoryView> guard(this);
        m_session->readMemory(m_address->text(), m_count->value(), [guard, generation](MemoryRead result) {
            if (guard && guard->m_generation == generation) guard->m_output->setPlainText(formatMemory(result));
        });
        QTimer::singleShot(20000, this, [this, generation] {
            if (generation == m_generation && m_output->toPlainText() == tr("Reading memory…"))
                m_output->setPlainText(tr("Memory request did not complete. Check debugger diagnostics."));
        });
    }
    DebuggerSession* m_session;
    QLineEdit* m_address;
    QSpinBox* m_count;
    QPlainTextEdit* m_output;
    QPushButton* m_readButton = nullptr;
    QPushButton* m_followButton = nullptr;
    bool m_historic = false;
    quint64 m_generation = 0;
};
