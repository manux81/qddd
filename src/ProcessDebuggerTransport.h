#include "DebuggerLogging.h"
#pragma once
#include "DebuggerTransport.h"
#include <QProcess>
class ProcessDebuggerTransport final : public DebuggerTransport {
public:
    explicit ProcessDebuggerTransport(QObject* parent=nullptr) : DebuggerTransport(parent) {
        connect(&process, &QProcess::readyReadStandardOutput, this, &DebuggerTransport::bytesReady);
        connect(&process, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) { emit finished(code,status==QProcess::CrashExit); });
        connect(&process, &QProcess::readyReadStandardError, this,
            [this] { emit diagnostic(QString::fromUtf8(process.readAllStandardError())); });
    }
    void start(const QString& program,const QStringList& args) override { qCDebug(debuggerTransportLog) << "start" << program; process.start(program,args); }
    bool waitForStarted(int ms) override { return process.waitForStarted(ms); }
    bool waitForFinished(int ms) override { return process.waitForFinished(ms); }
    bool isRunning() const override { return process.state()!=QProcess::NotRunning; }
    qint64 write(const QByteArray& bytes) override { return process.write(bytes); }
    QByteArray read() override { return process.readAllStandardOutput(); }
    QString errorString() const override { return process.errorString(); }
    void kill() override { process.kill(); }
private:
    QProcess process;
};
