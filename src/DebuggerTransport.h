#pragma once
#include <QObject>
#include <QByteArray>
#include <QStringList>

// A byte channel and process lifetime, without MI tokens or debugger-domain state.
class DebuggerTransport : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void start(const QString& program, const QStringList& arguments) = 0;
    virtual bool waitForStarted(int milliseconds) = 0;
    virtual bool waitForFinished(int milliseconds) = 0;
    virtual bool isRunning() const = 0;
    virtual qint64 write(const QByteArray& bytes) = 0;
    virtual QByteArray read() = 0;
    virtual QString errorString() const = 0;
    virtual void kill() = 0;
signals:
    void bytesReady();
    void finished(int exitCode, bool crashed);
    void diagnostic(const QString& message);
};
