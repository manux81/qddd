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

#include "MockGdbServer.h"

#include <QDebug>
#include <QCoreApplication>

// ============================================================================
// Constructor / Destructor
// ============================================================================

MockGdbServer::MockGdbServer(QObject* parent)
    : QObject(parent)
{
    m_delayTimer.setSingleShot(true);
    connect(&m_delayTimer, &QTimer::timeout, this, &MockGdbServer::onTimeout);
}

MockGdbServer::~MockGdbServer()
{
    stop();
}

// ============================================================================
// Start
// ============================================================================

void MockGdbServer::start()
{
    if (m_started)
        return;

    m_started = true;

    // Start TCP server if port is configured
    if (m_tcpPort > 0) {
        m_tcpServer = new QTcpServer(this);
        connect(m_tcpServer, &QTcpServer::newConnection,
                this, &MockGdbServer::onNewConnection);

        if (!m_tcpServer->listen(QHostAddress::LocalHost, m_tcpPort)) {
            qWarning() << "MockGdbServer: Failed to listen on port" << m_tcpPort;
            return;
        }
        qDebug() << "MockGdbServer: Listening on port" << m_tcpPort;
    }

    // If there's a startup delay, wait before writing output
    if (m_startupDelayMs > 0) {
        m_delayTimer.start(m_startupDelayMs);
    } else {
        // Write output immediately
        writeOutput();
    }

    emit started();
}

// ============================================================================
// Stop
// ============================================================================

void MockGdbServer::stop()
{
    m_delayTimer.stop();

    if (m_tcpServer) {
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }

    m_started = false;
    emit stopped();
}

// ============================================================================
// isRunning
// ============================================================================

bool MockGdbServer::isRunning() const
{
    return m_started;
}

// ============================================================================
// Timeout handler (startup delay)
// ============================================================================

void MockGdbServer::onTimeout()
{
    writeOutput();
}

// ============================================================================
// New TCP connection
// ============================================================================

void MockGdbServer::onNewConnection()
{
    // Accept and immediately close - just to confirm the port is open
    QTcpSocket* socket = m_tcpServer->nextPendingConnection();
    if (socket) {
        socket->disconnectFromHost();
        socket->deleteLater();
    }
}

// ============================================================================
// Write output
// ============================================================================

void MockGdbServer::writeOutput()
{
    // Write stdout lines
    for (const QString& line : m_stdoutLines) {
        qDebug().noquote() << "[MockGdbServer stdout]" << line;
    }

    // Write stderr lines
    for (const QString& line : m_stderrLines) {
        qDebug().noquote() << "[MockGdbServer stderr]" << line;
    }

    // Write ready message
    if (!m_readyMessage.isEmpty()) {
        qDebug().noquote() << "[MockGdbServer ready]" << m_readyMessage;
    }

    emit readyMessageWritten();
}