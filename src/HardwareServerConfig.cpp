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
 *    contributors may be used to promote or endorse products derived from this
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

#include "HardwareServerConfig.h"

#include <QFileInfo>
#include <QDir>

// ============================================================================
// Configuration key helpers (namespace-qualified to not pollute header)
// ============================================================================

namespace {

// Base key prefix for a single configuration entry
QString cfgKey(const QString& prefix, const QString& key)
{
    if (prefix.isEmpty())
        return QStringLiteral("hardwareDebug/") + key;
    return prefix + QStringLiteral("/") + key;
}

QStringList cfgListKey(const QString& prefix, const QString& key)
{
    QStringList keys;
    keys << cfgKey(prefix, key + QStringLiteral("/count"));
    keys << cfgKey(prefix, key + QStringLiteral("/%1"));
    return keys;
}

} // anonymous namespace

// ============================================================================
// Serialization
// ============================================================================

void HardwareDebugConfiguration::save(QSettings& s, const QString& prefix) const
{
    s.setValue(cfgKey(prefix, "name"), name);
    s.setValue(cfgKey(prefix, "enabled"), enabled);
    s.setValue(cfgKey(prefix, "serverType"), static_cast<int>(serverType));
    s.setValue(cfgKey(prefix, "gdbExecutable"), gdbExecutable);
    s.setValue(cfgKey(prefix, "serverExecutable"), serverExecutable);
    s.setValue(cfgKey(prefix, "workingDirectory"), workingDirectory);
    s.setValue(cfgKey(prefix, "host"), host);
    s.setValue(cfgKey(prefix, "port"), port);
    s.setValue(cfgKey(prefix, "serverArguments"), serverArguments);
    s.setValue(cfgKey(prefix, "programImage"), programImage);
    s.setValue(cfgKey(prefix, "symbolFile"), symbolFile);
    s.setValue(cfgKey(prefix, "loadImage"), loadImage);
    s.setValue(cfgKey(prefix, "loadSymbols"), loadSymbols);
    s.setValue(cfgKey(prefix, "resetBeforeLoad"), resetBeforeLoad);
    s.setValue(cfgKey(prefix, "haltAfterReset"), haltAfterReset);
    s.setValue(cfgKey(prefix, "runAfterLoad"), runAfterLoad);
    s.setValue(cfgKey(prefix, "initialBreakpoint"), initialBreakpoint);
    s.setValue(cfgKey(prefix, "startupTimeoutMs"), startupTimeoutMs);
    s.setValue(cfgKey(prefix, "shutdownTimeoutMs"), shutdownTimeoutMs);
    s.setValue(cfgKey(prefix, "preConnectCommands"), preConnectCommands);
    s.setValue(cfgKey(prefix, "postConnectCommands"), postConnectCommands);
    s.setValue(cfgKey(prefix, "preLoadCommands"), preLoadCommands);
    s.setValue(cfgKey(prefix, "postLoadCommands"), postLoadCommands);
    s.setValue(cfgKey(prefix, "readyPattern"), readyPattern);
    s.setValue(cfgKey(prefix, "stlinkCubeProgrammerPath"), stlinkCubeProgrammerPath);
    s.setValue(cfgKey(prefix, "stlinkLogLevel"), stlinkLogLevel);
    s.setValue(cfgKey(prefix, "stlinkSwdMode"), stlinkSwdMode);
    s.setValue(cfgKey(prefix, "stlinkSerialNumber"), stlinkSerialNumber);
    s.setValue(cfgKey(prefix, "jlinkDevice"), jlinkDevice);
    s.setValue(cfgKey(prefix, "jlinkInterface"), jlinkInterface);
    s.setValue(cfgKey(prefix, "jlinkSpeed"), jlinkSpeed);
    s.setValue(cfgKey(prefix, "jlinkTelnetPort"), jlinkTelnetPort);
    s.setValue(cfgKey(prefix, "jlinkSerialNumber"), jlinkSerialNumber);
    s.setValue(cfgKey(prefix, "jlinkEndianess"), jlinkEndianess);
}

void HardwareDebugConfiguration::load(const QSettings& s, const QString& prefix)
{
    name = s.value(cfgKey(prefix, "name"), name).toString();
    enabled = s.value(cfgKey(prefix, "enabled"), enabled).toBool();
    serverType = static_cast<HardwareServerType>(
        s.value(cfgKey(prefix, "serverType"), static_cast<int>(serverType)).toInt());
    gdbExecutable = s.value(cfgKey(prefix, "gdbExecutable"), gdbExecutable).toString();
    serverExecutable = s.value(cfgKey(prefix, "serverExecutable"), serverExecutable).toString();
    workingDirectory = s.value(cfgKey(prefix, "workingDirectory")).toString();
    host = s.value(cfgKey(prefix, "host"), host).toString();
    port = static_cast<quint16>(s.value(cfgKey(prefix, "port"), port).toUInt());
    serverArguments = s.value(cfgKey(prefix, "serverArguments")).toStringList();
    programImage = s.value(cfgKey(prefix, "programImage")).toString();
    symbolFile = s.value(cfgKey(prefix, "symbolFile")).toString();
    loadImage = s.value(cfgKey(prefix, "loadImage"), loadImage).toBool();
    loadSymbols = s.value(cfgKey(prefix, "loadSymbols"), loadSymbols).toBool();
    resetBeforeLoad = s.value(cfgKey(prefix, "resetBeforeLoad"), resetBeforeLoad).toBool();
    haltAfterReset = s.value(cfgKey(prefix, "haltAfterReset"), haltAfterReset).toBool();
    runAfterLoad = s.value(cfgKey(prefix, "runAfterLoad"), runAfterLoad).toBool();
    initialBreakpoint = s.value(cfgKey(prefix, "initialBreakpoint")).toString();
    startupTimeoutMs = s.value(cfgKey(prefix, "startupTimeoutMs"), startupTimeoutMs).toInt();
    shutdownTimeoutMs = s.value(cfgKey(prefix, "shutdownTimeoutMs"), shutdownTimeoutMs).toInt();
    preConnectCommands = s.value(cfgKey(prefix, "preConnectCommands")).toStringList();
    postConnectCommands = s.value(cfgKey(prefix, "postConnectCommands")).toStringList();
    preLoadCommands = s.value(cfgKey(prefix, "preLoadCommands")).toStringList();
    postLoadCommands = s.value(cfgKey(prefix, "postLoadCommands")).toStringList();
    readyPattern = s.value(cfgKey(prefix, "readyPattern")).toString();
    stlinkCubeProgrammerPath = s.value(cfgKey(prefix, "stlinkCubeProgrammerPath")).toString();
    stlinkLogLevel = s.value(cfgKey(prefix, "stlinkLogLevel"), stlinkLogLevel).toInt();
    stlinkSwdMode = s.value(cfgKey(prefix, "stlinkSwdMode"), stlinkSwdMode).toBool();
    stlinkSerialNumber = s.value(cfgKey(prefix, "stlinkSerialNumber")).toString();
    jlinkDevice = s.value(cfgKey(prefix, "jlinkDevice")).toString();
    jlinkInterface = s.value(cfgKey(prefix, "jlinkInterface"), jlinkInterface).toString();
    jlinkSpeed = s.value(cfgKey(prefix, "jlinkSpeed"), jlinkSpeed).toInt();
    jlinkTelnetPort = s.value(cfgKey(prefix, "jlinkTelnetPort"), jlinkTelnetPort).toInt();
    jlinkSerialNumber = s.value(cfgKey(prefix, "jlinkSerialNumber")).toString();
    jlinkEndianess = s.value(cfgKey(prefix, "jlinkEndianess")).toString();
}

// ============================================================================
// Validation
// ============================================================================

HardwareDebugConfiguration::ValidationResult HardwareDebugConfiguration::validate() const
{
    ValidationResult r;

    // GDB executable
    if (!gdbExecutable.isEmpty()) {
        QFileInfo fi(gdbExecutable);
        if (!fi.exists() || !fi.isExecutable()) {
            r.errors << QStringLiteral("GDB executable does not exist or is not executable: %1").arg(gdbExecutable);
            r.valid = false;
        }
    } else {
        r.errors << QStringLiteral("GDB executable path is empty.");
        r.valid = false;
    }

    // Server executable
    if (!serverExecutable.isEmpty()) {
        QFileInfo fi(serverExecutable);
        if (!fi.exists() || !fi.isExecutable()) {
            r.errors << QStringLiteral("Server executable does not exist or is not executable: %1").arg(serverExecutable);
            r.valid = false;
        }
    } else {
        r.errors << QStringLiteral("Server executable path is empty.");
        r.valid = false;
    }

    // Working directory (optional, but warn if set and doesn't exist)
    if (!workingDirectory.isEmpty()) {
        QDir dir(workingDirectory);
        if (!dir.exists()) {
            r.errors << QStringLiteral("Working directory does not exist: %1").arg(workingDirectory);
            r.valid = false;
        }
    }

    // Port
    if (port == 0) {
        r.errors << QStringLiteral("Port must be a positive value.");
        r.valid = false;
    }

    // Host
    if (host.trimmed().isEmpty()) {
        r.errors << QStringLiteral("Host cannot be empty.");
        r.valid = false;
    }

    // Program image
    if (loadImage || loadSymbols) {
        const QString img = programImage.trimmed();
        if (!img.isEmpty()) {
            QFileInfo fi(img);
            if (!fi.exists()) {
                r.errors << QStringLiteral("Program image does not exist: %1").arg(img);
                r.valid = false;
            }
        } else if (loadImage) {
            r.errors << QStringLiteral("Program image path is empty but load image is enabled.");
            r.valid = false;
        }
    }

    // Symbol file (optional, but check if specified)
    if (!symbolFile.isEmpty()) {
        QFileInfo fi(symbolFile);
        if (!fi.exists()) {
            r.errors << QStringLiteral("Symbol file does not exist: %1").arg(symbolFile);
            r.valid = false;
        }
    }

    // ST-Link specific
    if (serverType == HardwareServerType::STLink) {
        if (serverExecutable.isEmpty()) {
            r.errors << QStringLiteral("ST-LINK GDB server executable is required.");
            r.valid = false;
        }
    }

    // J-Link specific
    if (serverType == HardwareServerType::JLink) {
        if (jlinkDevice.trimmed().isEmpty()) {
            r.errors << QStringLiteral("J-Link device name is required.");
            r.valid = false;
        }
    }

    // Timeouts
    if (startupTimeoutMs < 1000) {
        r.errors << QStringLiteral("Startup timeout must be at least 1000 ms.");
        r.valid = false;
    }
    if (shutdownTimeoutMs < 500) {
        r.errors << QStringLiteral("Shutdown timeout must be at least 500 ms.");
        r.valid = false;
    }

    return r;
}

// ============================================================================
// Preset argument generation
// ============================================================================

QStringList HardwareDebugConfiguration::generateServerArguments() const
{
    switch (serverType) {
    case HardwareServerType::STLink:
        {
            QStringList args;
            args << QStringLiteral("-p") << QString::number(port);
            if (!stlinkCubeProgrammerPath.isEmpty()) {
                args << QStringLiteral("-cp") << stlinkCubeProgrammerPath;
            }
            args << QStringLiteral("-l") << QString::number(stlinkLogLevel);
            if (!stlinkSerialNumber.isEmpty()) {
                args << QStringLiteral("-sn") << stlinkSerialNumber;
            }
            if (stlinkSwdMode) {
                args << QStringLiteral("-s");
            }
            // Append any user-defined extra arguments
            args << serverArguments;
            return args;
        }
    case HardwareServerType::JLink:
        {
            QStringList args;
            if (!jlinkDevice.isEmpty()) {
                args << QStringLiteral("-device") << jlinkDevice;
            }
            if (!jlinkInterface.isEmpty()) {
                args << QStringLiteral("-if") << jlinkInterface;
            }
            if (jlinkSpeed > 0) {
                args << QStringLiteral("-speed") << QString::number(jlinkSpeed);
            }
            args << QStringLiteral("-port") << QString::number(port);
            if (jlinkTelnetPort > 0) {
                args << QStringLiteral("-telnetport") << QString::number(jlinkTelnetPort);
            }
            if (!jlinkSerialNumber.isEmpty()) {
                args << QStringLiteral("-USB") << jlinkSerialNumber;
            }
            if (!jlinkEndianess.isEmpty()) {
                args << QStringLiteral("-endian") << jlinkEndianess;
            }
            args << QStringLiteral("-singlerun");
            // Append any user-defined extra arguments
            args << serverArguments;
            return args;
        }
    case HardwareServerType::Generic:
    default:
        return serverArguments;
    }
}

// ============================================================================
// Default configuration
// ============================================================================

HardwareDebugConfiguration HardwareDebugConfiguration::defaultConfig()
{
    HardwareDebugConfiguration cfg;
    cfg.name = QStringLiteral("Default");
    cfg.enabled = false;
    cfg.serverType = HardwareServerType::Generic;
    cfg.gdbExecutable = QStringLiteral("gdb");
    cfg.serverExecutable.clear();
    cfg.workingDirectory.clear();
    cfg.host = QStringLiteral("127.0.0.1");
    cfg.port = 61234;
    cfg.serverArguments.clear();
    cfg.programImage.clear();
    cfg.symbolFile.clear();
    cfg.loadImage = true;
    cfg.loadSymbols = true;
    cfg.resetBeforeLoad = true;
    cfg.haltAfterReset = true;
    cfg.runAfterLoad = false;
    cfg.initialBreakpoint = QStringLiteral("main");
    cfg.startupTimeoutMs = 10000;
    cfg.shutdownTimeoutMs = 3000;
    cfg.readyPattern.clear();
    cfg.stlinkCubeProgrammerPath.clear();
    cfg.stlinkLogLevel = 1;
    cfg.stlinkSwdMode = true;
    cfg.stlinkSerialNumber.clear();
    cfg.jlinkDevice.clear();
    cfg.jlinkInterface = QStringLiteral("SWD");
    cfg.jlinkSpeed = 4000;
    cfg.jlinkTelnetPort = 0;
    cfg.jlinkSerialNumber.clear();
    cfg.jlinkEndianess.clear();
    return cfg;
}

// ============================================================================
// ConfigManager
// ============================================================================

void HardwareDebugConfigManager::save(QSettings& s) const
{
    // Save count and active index
    s.setValue(QStringLiteral("hardwareDebug/configCount"), configurations.size());
    s.setValue(QStringLiteral("hardwareDebug/activeIndex"), activeIndex);

    for (int i = 0; i < configurations.size(); ++i) {
        const QString prefix = QStringLiteral("hardwareDebug/config%1").arg(i);
        configurations[i].save(s, prefix);
    }
}

void HardwareDebugConfigManager::load(QSettings& s)
{
    const int count = s.value(QStringLiteral("hardwareDebug/configCount"), 0).toInt();
    activeIndex = s.value(QStringLiteral("hardwareDebug/activeIndex"), -1).toInt();

    configurations.clear();
    configurations.reserve(count);

    for (int i = 0; i < count; ++i) {
        const QString prefix = QStringLiteral("hardwareDebug/config%1").arg(i);
        HardwareDebugConfiguration cfg = HardwareDebugConfiguration::defaultConfig();
        cfg.load(s, prefix);
        configurations.append(cfg);
    }

    // Ensure at least one default config if none saved
    if (configurations.isEmpty()) {
        configurations.append(HardwareDebugConfiguration::defaultConfig());
        activeIndex = 0;
    }

    // Clamp active index
    if (activeIndex < 0 || activeIndex >= configurations.size()) {
        activeIndex = 0;
    }
}

const HardwareDebugConfiguration* HardwareDebugConfigManager::activeConfig() const
{
    if (activeIndex < 0 || activeIndex >= configurations.size())
        return nullptr;
    return &configurations[activeIndex];
}

HardwareDebugConfiguration* HardwareDebugConfigManager::activeConfig()
{
    if (activeIndex < 0 || activeIndex >= configurations.size())
        return nullptr;
    return &configurations[activeIndex];
}