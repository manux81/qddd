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

#include <QString>
#include <QStringList>
#include <QProcessEnvironment>
#include <QVector>
#include <QSettings>

// ============================================================================
// Hardware server type
// ============================================================================

enum class HardwareServerType {
    Generic,
    STLink,
    JLink
};

// ============================================================================
// Hardware debug configuration
// ============================================================================

struct HardwareDebugConfiguration
{
    // Identity
    QString name;
    bool enabled = false;

    // Server type
    HardwareServerType serverType = HardwareServerType::Generic;

    // Executables
    QString gdbExecutable;
    QString serverExecutable;
    QString workingDirectory;

    // Connection
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 61234;

    // Server arguments (separate from executable)
    QStringList serverArguments;
    QProcessEnvironment serverEnvironment;

    // Program image and symbols
    QString programImage;
    QString symbolFile;

    // Load options
    bool loadImage = true;
    bool loadSymbols = true;
    bool resetBeforeLoad = true;
    bool haltAfterReset = true;
    bool runAfterLoad = false;

    // Breakpoint
    QString initialBreakpoint;

    // Timeouts
    int startupTimeoutMs = 10000;
    int shutdownTimeoutMs = 3000;

    // Custom GDB/MI commands
    QStringList preConnectCommands;
    QStringList postConnectCommands;
    QStringList preLoadCommands;
    QStringList postLoadCommands;

    // Ready detection pattern (optional regex)
    QString readyPattern;

    // --- ST-Link specific fields ---
    QString stlinkCubeProgrammerPath;
    int stlinkLogLevel = 1;
    bool stlinkSwdMode = true;
    QString stlinkSerialNumber;

    // --- J-Link specific fields ---
    QString jlinkDevice;
    QString jlinkInterface = QStringLiteral("SWD");
    int jlinkSpeed = 4000;
    int jlinkTelnetPort = 0;
    QString jlinkSerialNumber;
    QString jlinkEndianess;

    // Serialization
    void save(QSettings& s, const QString& prefix = QString()) const;
    void load(const QSettings& s, const QString& prefix = QString());

    // Validation
    struct ValidationResult {
        bool valid = true;
        QStringList errors;
    };
    ValidationResult validate() const;

    // Preset argument generation
    QStringList generateServerArguments() const;
    QString effectiveStlinkCubeProgrammerPath() const;

    // Default configuration
    static HardwareDebugConfiguration defaultConfig();
};

// ============================================================================
// Container for multiple configurations
// ============================================================================

struct HardwareDebugConfigManager
{
    QVector<HardwareDebugConfiguration> configurations;
    int activeIndex = -1;

    void save(QSettings& s) const;
    void load(QSettings& s);

    [[nodiscard]] const HardwareDebugConfiguration* activeConfig() const;
    [[nodiscard]] HardwareDebugConfiguration* activeConfig();
};
