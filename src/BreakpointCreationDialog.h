#pragma once
#include "DebugSession.h"
#include <QDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
inline void showBreakpointCreation(DebuggerSession* session, QWidget* parent) {
    auto* dialog=new QDialog(parent); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("Add breakpoint / watchpoint"));
    auto* form=new QFormLayout(dialog);
    auto* kind=new QComboBox(dialog);
    kind->addItems({QObject::tr("Source"),QObject::tr("Function"),QObject::tr("Hardware"),QObject::tr("Write watchpoint"),QObject::tr("Read watchpoint"),QObject::tr("Access watchpoint"),QObject::tr("Catchpoint")});
    auto* location=new QLineEdit(dialog); location->setPlaceholderText(QObject::tr("file:line, function, expression, or catch event"));
    auto* condition=new QLineEdit(dialog);
    auto* ignore=new QSpinBox(dialog); ignore->setRange(0,1000000000);
    auto* temporary=new QCheckBox(dialog);
    form->addRow(QObject::tr("Kind"),kind); form->addRow(QObject::tr("Location / expression"),location);
    form->addRow(QObject::tr("Condition"),condition); form->addRow(QObject::tr("Ignore count"),ignore); form->addRow(QObject::tr("Temporary"),temporary);
    QObject::connect(kind,QOverload<int>::of(&QComboBox::currentIndexChanged),dialog,[=](int i) {
        temporary->setEnabled(i<3); if(i>=3) temporary->setChecked(false);
        condition->setEnabled(i!=6); ignore->setEnabled(i!=6);
        if(i==6) { condition->clear(); ignore->setValue(0); location->setPlaceholderText("throw, catch, fork, vfork, exec, load, unload"); }
    });
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog); form->addRow(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[=] {
        if(location->text().trimmed().isEmpty()) { location->setFocus(); return; }
        BreakpointRequest request; request.kind=static_cast<BreakpointRequest::Kind>(kind->currentIndex());
        request.location=location->text().trimmed(); request.condition=condition->text(); request.ignoreCount=ignore->value(); request.temporary=temporary->isChecked();
        session->insertBreakpoint(request); dialog->accept();
    });
    dialog->show();
}
