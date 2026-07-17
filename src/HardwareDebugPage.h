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

#include <QWidget>
#include <QVector>
#include <memory>

#include "HardwareServerConfig.h"

class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPlainTextEdit;
class QListWidget;
class QPushButton;
class QGroupBox;
class QStackedWidget;
class QLabel;
class GdbServerProcess;

// ============================================================================
// Hardware Debugging settings page
// ============================================================================

class HardwareDebugPage final : public QWidget
{
    Q_OBJECT

public:
    explicit HardwareDebugPage(QWidget* parent = nullptr);
    ~HardwareDebugPage() override;

    void load();
    void save();

    // Access to config manager for MainWindow
    HardwareDebugConfigManager& configManager() { return m_configManager; }
    const HardwareDebugConfigManager& configManager() const { return m_configManager; }

signals:
    void configChanged();

private slots:
    void onAddConfig();
    void onDeleteConfig();
    void onDuplicateConfig();
    void onRenameConfig();
    void onConfigSelected(int index);
    void onServerTypeChanged(int index);
    void onTestConfig();
    void onBrowseGdb();
    void onBrowseServer();
    void onBrowseWorkingDir();
    void onBrowseProgramImage();
    void onBrowseSymbolFile();
    void onBrowseCubeProgrammer();

private:
    void setupUi();
    void populateConfigList();
    void saveCurrentToConfig();
    void loadConfigToUi(int index);
    void updateServerTypeSpecificFields();
    void updateCommandPreview();

    // Config manager
    HardwareDebugConfigManager m_configManager;

    // Config list
    QListWidget* m_configList = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_duplicateBtn = nullptr;
    QPushButton* m_renameBtn = nullptr;

    // General settings
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_serverTypeCombo = nullptr;
    QLineEdit* m_gdbPathEdit = nullptr;
    QLineEdit* m_serverPathEdit = nullptr;
    QLineEdit* m_workingDirEdit = nullptr;
    QLineEdit* m_hostEdit = nullptr;
    QSpinBox* m_portSpin = nullptr;
    QSpinBox* m_startupTimeoutSpin = nullptr;
    QSpinBox* m_shutdownTimeoutSpin = nullptr;
    QLineEdit* m_readyPatternEdit = nullptr;
    QPlainTextEdit* m_serverArgsEdit = nullptr;

    // Image and symbols
    QLineEdit* m_programImageEdit = nullptr;
    QLineEdit* m_symbolFileEdit = nullptr;
    QCheckBox* m_loadImageCheck = nullptr;
    QCheckBox* m_loadSymbolsCheck = nullptr;
    QCheckBox* m_resetBeforeLoadCheck = nullptr;
    QCheckBox* m_haltAfterResetCheck = nullptr;
    QCheckBox* m_runAfterLoadCheck = nullptr;
    QLineEdit* m_initialBreakpointEdit = nullptr;

    // Custom commands
    QPlainTextEdit* m_preConnectEdit = nullptr;
    QPlainTextEdit* m_postConnectEdit = nullptr;
    QPlainTextEdit* m_preLoadEdit = nullptr;
    QPlainTextEdit* m_postLoadEdit = nullptr;

    // ST-Link specific
    QGroupBox* m_stlinkGroup = nullptr;
    QLineEdit* m_stlinkCubeProgPathEdit = nullptr;
    QSpinBox* m_stlinkLogLevelSpin = nullptr;
    QCheckBox* m_stlinkSwdCheck = nullptr;
    QLineEdit* m_stlinkSerialEdit = nullptr;

    // J-Link specific
    QGroupBox* m_jlinkGroup = nullptr;
    QLineEdit* m_jlinkDeviceEdit = nullptr;
    QComboBox* m_jlinkInterfaceCombo = nullptr;
    QSpinBox* m_jlinkSpeedSpin = nullptr;
    QSpinBox* m_jlinkTelnetPortSpin = nullptr;
    QLineEdit* m_jlinkSerialEdit = nullptr;
    QComboBox* m_jlinkEndianCombo = nullptr;

    // Command preview
    QLabel* m_commandPreview = nullptr;

    // Test button
    QPushButton* m_testBtn = nullptr;
    QLabel* m_testStatusLabel = nullptr;

    // Test server process
    std::unique_ptr<GdbServerProcess> m_testServer;
};