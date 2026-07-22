#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QTimer>

#include "HardwareServerConfig.h"

class MdbProcess final : public QObject
{
    Q_OBJECT

public:
    explicit MdbProcess(QObject* parent = nullptr);
    ~MdbProcess() override;

    void start(const HardwareDebugConfiguration& config);
    void stop();
    void sendCommand(const QString& command);
    [[nodiscard]] bool isRunning() const;

signals:
    void ready();
    void outputReceived(const QString& text);
    void errorOccurred(const QString& message);
    void finished(int exitCode, QProcess::ExitStatus status);

private:
    void onStarted();
    void onOutput();
    void onError(QProcess::ProcessError error);
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onTimeout();
    void queueBootstrapCommands();
    void dispatchNextCommand();
    bool outputHasPrompt() const;

    QProcess m_process;
    QTimer m_startupTimer;
    HardwareDebugConfiguration m_config;
    QQueue<QString> m_commands;
    QString m_outputBuffer;
    bool m_waitingForPrompt = false;
    bool m_ready = false;
    bool m_stopping = false;
};
