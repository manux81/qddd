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

#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QProcess>
#include <QTimer>
#include <QString>
#include <QStringList>

// ============================================================================
// Mock GDB Server for testing
// ============================================================================
// Simulates a GDB server that can:
//   - Write a ready message on stdout
//   - Open a TCP port
//   - Terminate with configurable exit code
//   - Ignore terminate to test kill fallback
//   - Delay startup
//   - Produce output on stdout and stderr
// ============================================================================

class MockGdbServer final : public QObject
{
    Q_OBJECT

public:
    explicit MockGdbServer(QObject* parent = nullptr);
    ~MockGdbServer() override;

    // Start the mock server
    void start();

    // Stop the mock server
    void stop();

    // Configuration
    void setReadyMessage(const QString& msg) { m_readyMessage = msg; }
    void setReadyPattern(const QString& pattern) { m_readyPattern = pattern; }
    void setExitCode(int code) { m_exitCode = code; }
    void setStartupDelay(int ms) { m_startupDelayMs = ms; }
    void setIgnoreTerminate(bool ignore) { m_ignoreTerminate = ignore; }
    void setTcpPort(quint16 port) { m_tcpPort = port; }
    void setStdoutLines(const QStringList& lines) { m_stdoutLines = lines; }
    void setStderrLines(const QStringList& lines) { m_stderrLines = lines; }

    // Accessors
    [[nodiscard]] quint16 tcpPort() const { return m_tcpPort; }
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] int exitCode() const { return m_exitCode; }

signals:
    void started();
    void stopped();
    void readyMessageWritten();

private slots:
    void onTimeout();
    void onNewConnection();

private:
    void writeOutput();

    // TCP server for simulating GDB server port
    QTcpServer* m_tcpServer = nullptr;

    // Process for simulating a real external process
    QProcess* m_process = nullptr;

    // Configuration
    QString m_readyMessage = QStringLiteral("Waiting for connection...");
    QString m_readyPattern;
    int m_exitCode = 0;
    int m_startupDelayMs = 0;
    bool m_ignoreTerminate = false;
    quint16 m_tcpPort = 0;

    // Output lines
    QStringList m_stdoutLines;
    QStringList m_stderrLines;

    // Timer for startup delay
    QTimer m_delayTimer;
    bool m_started = false;
};