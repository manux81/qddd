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

#include "HardwareDebugPage.h"
#include "GdbServerProcess.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

// ============================================================================
// Constructor / Destructor
// ============================================================================

HardwareDebugPage::HardwareDebugPage(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

HardwareDebugPage::~HardwareDebugPage() = default;

// ============================================================================
// UI Setup
// ============================================================================

void HardwareDebugPage::setupUi()
{
    auto* rootLayout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Create one profile for each hardware probe. Enabled profiles appear in the target selector in the main window. Changes are stored with Apply or OK."),
        this);
    intro->setWordWrap(true);
    intro->setStyleSheet("padding: 8px; background: rgba(70,120,180,35); border-radius: 5px;");
    rootLayout->addWidget(intro);

    // ================================================================
    // Top: Configuration list management
    // ================================================================
    auto* configListBox = new QGroupBox(tr("Hardware probe profiles"), this);
    auto* configListLayout = new QVBoxLayout(configListBox);

    m_configList = new QListWidget(configListBox);
    m_configList->setMaximumHeight(120);
    connect(m_configList, &QListWidget::currentRowChanged,
            this, &HardwareDebugPage::onConfigSelected);

    auto* configBtnLayout = new QHBoxLayout();
    m_addBtn = new QPushButton(tr("New profile"), configListBox);
    m_deleteBtn = new QPushButton(tr("Delete profile"), configListBox);
    m_duplicateBtn = new QPushButton(tr("Duplicate"), configListBox);
    m_renameBtn = new QPushButton(tr("Rename"), configListBox);

    connect(m_addBtn, &QPushButton::clicked, this, &HardwareDebugPage::onAddConfig);
    connect(m_deleteBtn, &QPushButton::clicked, this, &HardwareDebugPage::onDeleteConfig);
    connect(m_duplicateBtn, &QPushButton::clicked, this, &HardwareDebugPage::onDuplicateConfig);
    connect(m_renameBtn, &QPushButton::clicked, this, &HardwareDebugPage::onRenameConfig);

    configBtnLayout->addWidget(m_addBtn);
    configBtnLayout->addWidget(m_deleteBtn);
    configBtnLayout->addWidget(m_duplicateBtn);
    configBtnLayout->addWidget(m_renameBtn);
    configBtnLayout->addStretch(1);

    configListLayout->addWidget(m_configList);
    configListLayout->addLayout(configBtnLayout);

    rootLayout->addWidget(configListBox);

    // ================================================================
    // Scroll area for all configuration fields
    // ================================================================
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    auto* scrollContent = new QWidget(scrollArea);
    auto* scrollLayout = new QVBoxLayout(scrollContent);

    // ================================================================
    // General settings
    // ================================================================
    auto* generalBox = new QGroupBox(tr("General Configuration"), scrollContent);
    auto* generalForm = new QFormLayout(generalBox);

    m_nameEdit = new QLineEdit(generalBox);
    m_nameEdit->setPlaceholderText(tr("Configuration name"));
    generalForm->addRow(tr("Name:"), m_nameEdit);

    m_enabledCheck = new QCheckBox(tr("Show this configuration in the main debugger selector"), generalBox);
    generalForm->addRow(QString(), m_enabledCheck);

    m_serverTypeCombo = new QComboBox(generalBox);
    m_serverTypeCombo->addItem(tr("Generic GDB Server"), "generic");
    m_serverTypeCombo->addItem(tr("ST-LINK"), "stlink");
    m_serverTypeCombo->addItem(tr("J-Link"), "jlink");
    connect(m_serverTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HardwareDebugPage::onServerTypeChanged);
    generalForm->addRow(tr("Server type:"), m_serverTypeCombo);

    // GDB executable
    auto* gdbRow = new QWidget(generalBox);
    auto* gdbRowLayout = new QHBoxLayout(gdbRow);
    gdbRowLayout->setContentsMargins(0, 0, 0, 0);
    m_gdbPathEdit = new QLineEdit(gdbRow);
    m_gdbPathEdit->setPlaceholderText(tr("arm-none-eabi-gdb"));
    auto* gdbBrowse = new QPushButton(tr("Browse..."), gdbRow);
    connect(gdbBrowse, &QPushButton::clicked, this, &HardwareDebugPage::onBrowseGdb);
    gdbRowLayout->addWidget(m_gdbPathEdit, 1);
    gdbRowLayout->addWidget(gdbBrowse);
    generalForm->addRow(tr("GDB executable:"), gdbRow);

    // Server executable
    auto* serverRow = new QWidget(generalBox);
    auto* serverRowLayout = new QHBoxLayout(serverRow);
    serverRowLayout->setContentsMargins(0, 0, 0, 0);
    m_serverPathEdit = new QLineEdit(serverRow);
    m_serverPathEdit->setPlaceholderText(tr("/path/to/gdb-server"));
    auto* serverBrowse = new QPushButton(tr("Browse..."), serverRow);
    connect(serverBrowse, &QPushButton::clicked, this, &HardwareDebugPage::onBrowseServer);
    serverRowLayout->addWidget(m_serverPathEdit, 1);
    serverRowLayout->addWidget(serverBrowse);
    generalForm->addRow(tr("Server executable:"), serverRow);

    // Working directory
    auto* wdRow = new QWidget(generalBox);
    auto* wdRowLayout = new QHBoxLayout(wdRow);
    wdRowLayout->setContentsMargins(0, 0, 0, 0);
    m_workingDirEdit = new QLineEdit(wdRow);
    m_workingDirEdit->setPlaceholderText(tr("Optional working directory"));
    auto* wdBrowse = new QPushButton(tr("Browse..."), wdRow);
    connect(wdBrowse, &QPushButton::clicked, this, &HardwareDebugPage::onBrowseWorkingDir);
    wdRowLayout->addWidget(m_workingDirEdit, 1);
    wdRowLayout->addWidget(wdBrowse);
    generalForm->addRow(tr("Working directory:"), wdRow);

    // Host and port
    m_hostEdit = new QLineEdit(generalBox);
    m_hostEdit->setPlaceholderText("127.0.0.1");
    generalForm->addRow(tr("Host:"), m_hostEdit);

    m_portSpin = new QSpinBox(generalBox);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(61234);
    generalForm->addRow(tr("Port:"), m_portSpin);

    // Timeouts
    m_startupTimeoutSpin = new QSpinBox(generalBox);
    m_startupTimeoutSpin->setRange(1000, 120000);
    m_startupTimeoutSpin->setValue(10000);
    m_startupTimeoutSpin->setSuffix(tr(" ms"));
    generalForm->addRow(tr("Startup timeout:"), m_startupTimeoutSpin);

    m_shutdownTimeoutSpin = new QSpinBox(generalBox);
    m_shutdownTimeoutSpin->setRange(500, 60000);
    m_shutdownTimeoutSpin->setValue(3000);
    m_shutdownTimeoutSpin->setSuffix(tr(" ms"));
    generalForm->addRow(tr("Shutdown timeout:"), m_shutdownTimeoutSpin);

    // Ready pattern
    m_readyPatternEdit = new QLineEdit(generalBox);
    m_readyPatternEdit->setPlaceholderText(tr("Optional regex pattern (e.g., Waiting for connection)"));
    generalForm->addRow(tr("Ready pattern:"), m_readyPatternEdit);

    // Extra server arguments
    m_serverArgsEdit = new QPlainTextEdit(generalBox);
    m_serverArgsEdit->setPlaceholderText(tr("One argument per line"));
    m_serverArgsEdit->setMaximumHeight(80);
    generalForm->addRow(tr("Extra arguments:"), m_serverArgsEdit);

    scrollLayout->addWidget(generalBox);

    // ================================================================
    // Command preview
    // ================================================================
    auto* previewBox = new QGroupBox(tr("Command Preview (read-only)"), scrollContent);
    auto* previewLayout = new QVBoxLayout(previewBox);
    m_commandPreview = new QLabel(previewBox);
    m_commandPreview->setWordWrap(true);
    m_commandPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_commandPreview->setStyleSheet("background: #2d2d2d; padding: 8px; border-radius: 4px; font-family: monospace;");
    previewLayout->addWidget(m_commandPreview);
    scrollLayout->addWidget(previewBox);

    // ================================================================
    // Image and symbols
    // ================================================================
    auto* imageBox = new QGroupBox(tr("Program Image && Symbols"), scrollContent);
    auto* imageForm = new QFormLayout(imageBox);

    auto* progRow = new QWidget(imageBox);
    auto* progRowLayout = new QHBoxLayout(progRow);
    progRowLayout->setContentsMargins(0, 0, 0, 0);
    m_programImageEdit = new QLineEdit(progRow);
    m_programImageEdit->setPlaceholderText(tr("/path/to/firmware.elf"));
    auto* progBrowse = new QPushButton(tr("Browse..."), progRow);
    connect(progBrowse, &QPushButton::clicked, this, &HardwareDebugPage::onBrowseProgramImage);
    progRowLayout->addWidget(m_programImageEdit, 1);
    progRowLayout->addWidget(progBrowse);
    imageForm->addRow(tr("Program image:"), progRow);

    auto* symRow = new QWidget(imageBox);
    auto* symRowLayout = new QHBoxLayout(symRow);
    symRowLayout->setContentsMargins(0, 0, 0, 0);
    m_symbolFileEdit = new QLineEdit(symRow);
    m_symbolFileEdit->setPlaceholderText(tr("Leave empty to use program image"));
    auto* symBrowse = new QPushButton(tr("Browse..."), symRow);
    connect(symBrowse, &QPushButton::clicked, this, &HardwareDebugPage::onBrowseSymbolFile);
    symRowLayout->addWidget(m_symbolFileEdit, 1);
    symRowLayout->addWidget(symBrowse);
    imageForm->addRow(tr("Symbol file:"), symRow);

    m_loadImageCheck = new QCheckBox(tr("Download image to target"), imageBox);
    m_loadImageCheck->setChecked(true);
    imageForm->addRow(QString(), m_loadImageCheck);

    m_loadSymbolsCheck = new QCheckBox(tr("Load symbols"), imageBox);
    m_loadSymbolsCheck->setChecked(true);
    imageForm->addRow(QString(), m_loadSymbolsCheck);

    m_resetBeforeLoadCheck = new QCheckBox(tr("Reset target before load"), imageBox);
    m_resetBeforeLoadCheck->setChecked(true);
    imageForm->addRow(QString(), m_resetBeforeLoadCheck);

    m_haltAfterResetCheck = new QCheckBox(tr("Halt target after reset"), imageBox);
    m_haltAfterResetCheck->setChecked(true);
    imageForm->addRow(QString(), m_haltAfterResetCheck);

    m_runAfterLoadCheck = new QCheckBox(tr("Run target after load"), imageBox);
    m_runAfterLoadCheck->setChecked(false);
    imageForm->addRow(QString(), m_runAfterLoadCheck);

    m_initialBreakpointEdit = new QLineEdit(imageBox);
    m_initialBreakpointEdit->setPlaceholderText(tr("main"));
    imageForm->addRow(tr("Initial breakpoint:"), m_initialBreakpointEdit);

    scrollLayout->addWidget(imageBox);

    // ================================================================
    // ST-Link specific settings
    // ================================================================
    m_stlinkGroup = new QGroupBox(tr("ST-LINK Specific"), scrollContent);
    auto* stlinkForm = new QFormLayout(m_stlinkGroup);

    auto* cubeRow = new QWidget(m_stlinkGroup);
    auto* cubeRowLayout = new QHBoxLayout(cubeRow);
    cubeRowLayout->setContentsMargins(0, 0, 0, 0);
    m_stlinkCubeProgPathEdit = new QLineEdit(cubeRow);
    m_stlinkCubeProgPathEdit->setPlaceholderText(tr("/path/to/STM32CubeProgrammer/bin"));
    auto* cubeBrowse = new QPushButton(tr("Browse..."), cubeRow);
    connect(cubeBrowse, &QPushButton::clicked, this, &HardwareDebugPage::onBrowseCubeProgrammer);
    cubeRowLayout->addWidget(m_stlinkCubeProgPathEdit, 1);
    cubeRowLayout->addWidget(cubeBrowse);
    stlinkForm->addRow(tr("CubeProgrammer path:"), cubeRow);

    m_stlinkLogLevelSpin = new QSpinBox(m_stlinkGroup);
    m_stlinkLogLevelSpin->setRange(0, 31);
    m_stlinkLogLevelSpin->setValue(1);
    stlinkForm->addRow(tr("Log level:"), m_stlinkLogLevelSpin);

    m_stlinkSwdCheck = new QCheckBox(tr("SWD mode (uncheck for JTAG)"), m_stlinkGroup);
    m_stlinkSwdCheck->setChecked(true);
    stlinkForm->addRow(QString(), m_stlinkSwdCheck);

    m_stlinkSerialEdit = new QLineEdit(m_stlinkGroup);
    m_stlinkSerialEdit->setPlaceholderText(tr("Optional probe serial number"));
    stlinkForm->addRow(tr("Serial number:"), m_stlinkSerialEdit);

    m_stlinkGroup->setVisible(false);
    scrollLayout->addWidget(m_stlinkGroup);

    // ================================================================
    // J-Link specific settings
    // ================================================================
    m_jlinkGroup = new QGroupBox(tr("J-Link Specific"), scrollContent);
    auto* jlinkForm = new QFormLayout(m_jlinkGroup);

    m_jlinkDeviceEdit = new QLineEdit(m_jlinkGroup);
    m_jlinkDeviceEdit->setPlaceholderText(tr("e.g., STM32F407VG"));
    jlinkForm->addRow(tr("Device:"), m_jlinkDeviceEdit);

    m_jlinkInterfaceCombo = new QComboBox(m_jlinkGroup);
    m_jlinkInterfaceCombo->addItem("SWD", "SWD");
    m_jlinkInterfaceCombo->addItem("JTAG", "JTAG");
    jlinkForm->addRow(tr("Interface:"), m_jlinkInterfaceCombo);

    m_jlinkSpeedSpin = new QSpinBox(m_jlinkGroup);
    m_jlinkSpeedSpin->setRange(1, 12000);
    m_jlinkSpeedSpin->setValue(4000);
    m_jlinkSpeedSpin->setSuffix(tr(" kHz"));
    jlinkForm->addRow(tr("Speed:"), m_jlinkSpeedSpin);

    m_jlinkTelnetPortSpin = new QSpinBox(m_jlinkGroup);
    m_jlinkTelnetPortSpin->setRange(0, 65535);
    m_jlinkTelnetPortSpin->setValue(0);
    m_jlinkTelnetPortSpin->setSpecialValueText(tr("Disabled"));
    jlinkForm->addRow(tr("Telnet port:"), m_jlinkTelnetPortSpin);

    m_jlinkSerialEdit = new QLineEdit(m_jlinkGroup);
    m_jlinkSerialEdit->setPlaceholderText(tr("Optional serial number"));
    jlinkForm->addRow(tr("Serial number:"), m_jlinkSerialEdit);

    m_jlinkEndianCombo = new QComboBox(m_jlinkGroup);
    m_jlinkEndianCombo->addItem(tr("Little"), "little");
    m_jlinkEndianCombo->addItem(tr("Big"), "big");
    m_jlinkEndianCombo->setCurrentIndex(0);
    jlinkForm->addRow(tr("Endianess:"), m_jlinkEndianCombo);

    m_jlinkGroup->setVisible(false);
    scrollLayout->addWidget(m_jlinkGroup);

    // ================================================================
    // Custom commands
    // ================================================================
    auto* cmdBox = new QGroupBox(tr("Custom GDB/MI Commands"), scrollContent);
    auto* cmdLayout = new QVBoxLayout(cmdBox);

    auto addCmdGroup = [&](const QString& title, QPlainTextEdit*& edit, const QString& placeholder) {
        auto* groupLabel = new QLabel(title, cmdBox);
        groupLabel->setStyleSheet("font-weight: bold; margin-top: 8px;");
        edit = new QPlainTextEdit(cmdBox);
        edit->setPlaceholderText(placeholder);
        edit->setMaximumHeight(80);
        cmdLayout->addWidget(groupLabel);
        cmdLayout->addWidget(edit);
    };

    addCmdGroup(tr("Pre-connect commands (one per line):"), m_preConnectEdit,
                tr("e.g.\n-monitor reset\nset mem inaccessible-by-default off"));
    addCmdGroup(tr("Post-connect commands (one per line):"), m_postConnectEdit,
                tr("e.g.\n-monitor reset halt"));
    addCmdGroup(tr("Pre-load commands (one per line):"), m_preLoadEdit,
                tr("Optional commands before -target-download"));
    addCmdGroup(tr("Post-load commands (one per line):"), m_postLoadEdit,
                tr("Optional commands after -target-download"));

    auto* cmdInfo = new QLabel(
        tr("Each line is sent as a GDB/MI command. "
           "Use -interpreter-exec console \"...\" for CLI commands."),
        cmdBox);
    cmdInfo->setWordWrap(true);
    cmdInfo->setStyleSheet("color: rgba(255,255,255,140);");
    cmdLayout->addWidget(cmdInfo);

    scrollLayout->addWidget(cmdBox);

    // ================================================================
    // Test configuration
    // ================================================================
    auto* testBox = new QGroupBox(tr("Test Configuration"), scrollContent);
    auto* testLayout = new QVBoxLayout(testBox);

    auto* testBtnLayout = new QHBoxLayout();
    m_testBtn = new QPushButton(tr("Test Configuration"), testBox);
    connect(m_testBtn, &QPushButton::clicked, this, &HardwareDebugPage::onTestConfig);
    testBtnLayout->addWidget(m_testBtn);
    testBtnLayout->addStretch(1);

    m_testStatusLabel = new QLabel(testBox);
    m_testStatusLabel->setWordWrap(true);
    m_testStatusLabel->setStyleSheet("color: rgba(255,255,255,140); padding: 4px;");

    testLayout->addLayout(testBtnLayout);
    testLayout->addWidget(m_testStatusLabel);

    scrollLayout->addWidget(testBox);
    scrollLayout->addStretch(1);

    scrollArea->setWidget(scrollContent);
    rootLayout->addWidget(scrollArea, 1);
}

// ============================================================================
// Load / Save
// ============================================================================

void HardwareDebugPage::load()
{
    QSettings s;
    m_configManager.load(s);
    populateConfigList();

    if (!m_configManager.configurations.isEmpty()) {
        m_configList->setCurrentRow(m_configManager.activeIndex);
    }
}

void HardwareDebugPage::save()
{
    // Flush current UI to config manager before persisting
    saveCurrentToConfig();
    QSettings s;
    m_configManager.save(s);
    s.sync();
    const int selected = m_loadedIndex;
    populateConfigList();
    if (selected >= 0 && selected < m_configList->count())
        m_configList->setCurrentRow(selected);
}

// ============================================================================
// Config list management
// ============================================================================

void HardwareDebugPage::populateConfigList()
{
    m_configList->blockSignals(true);
    m_configList->clear();
    for (const auto& cfg : m_configManager.configurations) {
        QString display = cfg.name;
        if (cfg.enabled)
            display += tr("  • visible in main window");
        m_configList->addItem(display);
    }
    m_configList->blockSignals(false);
}

void HardwareDebugPage::onAddConfig()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Configuration"),
                                                tr("Configuration name:"),
                                                QLineEdit::Normal,
                                                tr("New Config"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    HardwareDebugConfiguration cfg = HardwareDebugConfiguration::defaultConfig();
    cfg.name = name.trimmed();
    cfg.enabled = true;

    m_configManager.configurations.append(cfg);
    m_configManager.activeIndex = m_configManager.configurations.size() - 1;
    populateConfigList();
    m_configList->setCurrentRow(m_configManager.activeIndex);
    emit configChanged();
}

void HardwareDebugPage::onDeleteConfig()
{
    const int idx = m_loadedIndex;
    if (idx < 0 || idx >= m_configManager.configurations.size())
        return;

    auto reply = QMessageBox::question(this, tr("Delete Configuration"),
                                        tr("Delete \"%1\"?").arg(m_configManager.configurations[idx].name));
    if (reply != QMessageBox::Yes)
        return;

    m_configManager.configurations.removeAt(idx);
    m_loadedIndex = -1;
    if (!m_configManager.configurations.isEmpty()) {
        m_configManager.activeIndex = qMin(idx, m_configManager.configurations.size() - 1);
    } else {
        m_configManager.activeIndex = -1;
    }
    populateConfigList();
    if (m_configManager.activeIndex >= 0) {
        m_configList->setCurrentRow(m_configManager.activeIndex);
    }
    emit configChanged();
}

void HardwareDebugPage::onDuplicateConfig()
{
    const int idx = m_configList->currentRow();
    if (idx < 0 || idx >= m_configManager.configurations.size())
        return;

    HardwareDebugConfiguration cfg = m_configManager.configurations[idx];
    cfg.name = cfg.name + QStringLiteral(" (copy)");

    m_configManager.configurations.append(cfg);
    m_configManager.activeIndex = m_configManager.configurations.size() - 1;
    populateConfigList();
    m_configList->setCurrentRow(m_configManager.activeIndex);
    emit configChanged();
}

void HardwareDebugPage::onRenameConfig()
{
    const int idx = m_configList->currentRow();
    if (idx < 0 || idx >= m_configManager.configurations.size())
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Configuration"),
                                                tr("New name:"),
                                                QLineEdit::Normal,
                                                m_configManager.configurations[idx].name, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    m_configManager.configurations[idx].name = name.trimmed();
    populateConfigList();
    m_configList->setCurrentRow(idx);
    emit configChanged();
}

void HardwareDebugPage::onConfigSelected(int index)
{
    if (index < 0 || index >= m_configManager.configurations.size())
        return;

    // Save the previously displayed profile before switching.
    saveCurrentToConfig();

    m_configManager.activeIndex = index;
    loadConfigToUi(index);
    updateServerTypeSpecificFields();
    updateCommandPreview();
}

// ============================================================================
// UI <-> Config synchronization
// ============================================================================

void HardwareDebugPage::saveCurrentToConfig()
{
    const int idx = m_loadedIndex;
    if (idx < 0 || idx >= m_configManager.configurations.size())
        return;

    HardwareDebugConfiguration& cfg = m_configManager.configurations[idx];
    cfg.name = m_nameEdit->text().trimmed();
    cfg.enabled = m_enabledCheck->isChecked();
    cfg.serverType = static_cast<HardwareServerType>(m_serverTypeCombo->currentIndex());
    cfg.gdbExecutable = m_gdbPathEdit->text().trimmed();
    cfg.serverExecutable = m_serverPathEdit->text().trimmed();
    cfg.workingDirectory = m_workingDirEdit->text().trimmed();
    cfg.host = m_hostEdit->text().trimmed();
    cfg.port = static_cast<quint16>(m_portSpin->value());
    cfg.startupTimeoutMs = m_startupTimeoutSpin->value();
    cfg.shutdownTimeoutMs = m_shutdownTimeoutSpin->value();
    cfg.readyPattern = m_readyPatternEdit->text().trimmed();
    cfg.serverArguments = m_serverArgsEdit->toPlainText().split(QStringLiteral("\n"),
                                                                 Qt::SkipEmptyParts);

    cfg.programImage = m_programImageEdit->text().trimmed();
    cfg.symbolFile = m_symbolFileEdit->text().trimmed();
    cfg.loadImage = m_loadImageCheck->isChecked();
    cfg.loadSymbols = m_loadSymbolsCheck->isChecked();
    cfg.resetBeforeLoad = m_resetBeforeLoadCheck->isChecked();
    cfg.haltAfterReset = m_haltAfterResetCheck->isChecked();
    cfg.runAfterLoad = m_runAfterLoadCheck->isChecked();
    cfg.initialBreakpoint = m_initialBreakpointEdit->text().trimmed();

    cfg.preConnectCommands = m_preConnectEdit->toPlainText().split(QStringLiteral("\n"),
                                                                    Qt::SkipEmptyParts);
    cfg.postConnectCommands = m_postConnectEdit->toPlainText().split(QStringLiteral("\n"),
                                                                      Qt::SkipEmptyParts);
    cfg.preLoadCommands = m_preLoadEdit->toPlainText().split(QStringLiteral("\n"),
                                                              Qt::SkipEmptyParts);
    cfg.postLoadCommands = m_postLoadEdit->toPlainText().split(QStringLiteral("\n"),
                                                                Qt::SkipEmptyParts);

    // ST-Link
    cfg.stlinkCubeProgrammerPath = m_stlinkCubeProgPathEdit->text().trimmed();
    cfg.stlinkLogLevel = m_stlinkLogLevelSpin->value();
    cfg.stlinkSwdMode = m_stlinkSwdCheck->isChecked();
    cfg.stlinkSerialNumber = m_stlinkSerialEdit->text().trimmed();

    // J-Link
    cfg.jlinkDevice = m_jlinkDeviceEdit->text().trimmed();
    cfg.jlinkInterface = m_jlinkInterfaceCombo->currentData().toString();
    cfg.jlinkSpeed = m_jlinkSpeedSpin->value();
    cfg.jlinkTelnetPort = m_jlinkTelnetPortSpin->value();
    cfg.jlinkSerialNumber = m_jlinkSerialEdit->text().trimmed();
    cfg.jlinkEndianess = m_jlinkEndianCombo->currentData().toString();
}

void HardwareDebugPage::loadConfigToUi(int index)
{
    if (index < 0 || index >= m_configManager.configurations.size())
        return;

    m_loadingUi = true;
    const HardwareDebugConfiguration cfg = m_configManager.configurations[index];
    m_loadedIndex = index;
    m_nameEdit->setText(cfg.name);
    m_enabledCheck->setChecked(cfg.enabled);

    const int typeIdx = static_cast<int>(cfg.serverType);
    m_serverTypeCombo->setCurrentIndex(typeIdx >= 0 && typeIdx < m_serverTypeCombo->count()
                                       ? typeIdx : 0);
    m_gdbPathEdit->setText(cfg.gdbExecutable);
    m_serverPathEdit->setText(cfg.serverExecutable);
    m_workingDirEdit->setText(cfg.workingDirectory);
    m_hostEdit->setText(cfg.host);
    m_portSpin->setValue(cfg.port);
    m_startupTimeoutSpin->setValue(cfg.startupTimeoutMs);
    m_shutdownTimeoutSpin->setValue(cfg.shutdownTimeoutMs);
    m_readyPatternEdit->setText(cfg.readyPattern);
    m_serverArgsEdit->setPlainText(cfg.serverArguments.join(QStringLiteral("\n")));

    m_programImageEdit->setText(cfg.programImage);
    m_symbolFileEdit->setText(cfg.symbolFile);
    m_loadImageCheck->setChecked(cfg.loadImage);
    m_loadSymbolsCheck->setChecked(cfg.loadSymbols);
    m_resetBeforeLoadCheck->setChecked(cfg.resetBeforeLoad);
    m_haltAfterResetCheck->setChecked(cfg.haltAfterReset);
    m_runAfterLoadCheck->setChecked(cfg.runAfterLoad);
    m_initialBreakpointEdit->setText(cfg.initialBreakpoint);

    m_preConnectEdit->setPlainText(cfg.preConnectCommands.join(QStringLiteral("\n")));
    m_postConnectEdit->setPlainText(cfg.postConnectCommands.join(QStringLiteral("\n")));
    m_preLoadEdit->setPlainText(cfg.preLoadCommands.join(QStringLiteral("\n")));
    m_postLoadEdit->setPlainText(cfg.postLoadCommands.join(QStringLiteral("\n")));

    // ST-Link
    m_stlinkCubeProgPathEdit->setText(cfg.stlinkCubeProgrammerPath);
    m_stlinkLogLevelSpin->setValue(cfg.stlinkLogLevel);
    m_stlinkSwdCheck->setChecked(cfg.stlinkSwdMode);
    m_stlinkSerialEdit->setText(cfg.stlinkSerialNumber);

    // J-Link
    m_jlinkDeviceEdit->setText(cfg.jlinkDevice);
    const int ifIdx = m_jlinkInterfaceCombo->findData(cfg.jlinkInterface);
    m_jlinkInterfaceCombo->setCurrentIndex(ifIdx >= 0 ? ifIdx : 0);
    m_jlinkSpeedSpin->setValue(cfg.jlinkSpeed);
    m_jlinkTelnetPortSpin->setValue(cfg.jlinkTelnetPort);
    m_jlinkSerialEdit->setText(cfg.jlinkSerialNumber);
    const int endIdx = m_jlinkEndianCombo->findData(cfg.jlinkEndianess);
    m_jlinkEndianCombo->setCurrentIndex(endIdx >= 0 ? endIdx : 0);
    m_loadingUi = false;
}

void HardwareDebugPage::onServerTypeChanged(int /*index*/)
{
    updateServerTypeSpecificFields();
    if (m_loadingUi)
        return;
    updateCommandPreview();
}

void HardwareDebugPage::updateServerTypeSpecificFields()
{
    const int typeIdx = m_serverTypeCombo->currentIndex();
    const auto type = static_cast<HardwareServerType>(typeIdx);

    m_stlinkGroup->setVisible(type == HardwareServerType::STLink);
    m_jlinkGroup->setVisible(type == HardwareServerType::JLink);
}

void HardwareDebugPage::updateCommandPreview()
{
    if (m_loadingUi)
        return;
    saveCurrentToConfig();

    const int idx = m_configList->currentRow();
    if (idx < 0 || idx >= m_configManager.configurations.size()) {
        m_commandPreview->setText(QString());
        return;
    }

    HardwareDebugConfiguration cfg = m_configManager.configurations[idx];

    QString preview;
    preview += QStringLiteral("Server executable:\n  %1\n\n").arg(cfg.serverExecutable);
    preview += QStringLiteral("Arguments:\n");
    const QStringList args = cfg.generateServerArguments();
    for (const auto& arg : args) {
        preview += QStringLiteral("  %1\n").arg(arg);
    }
    preview += QStringLiteral("\nHost: %1\n").arg(cfg.host);
    preview += QStringLiteral("Port: %1\n\n").arg(cfg.port);
    preview += QStringLiteral("GDB executable:\n  %1\n\n").arg(cfg.gdbExecutable);
    if (!cfg.workingDirectory.isEmpty()) {
        preview += QStringLiteral("Working directory:\n  %1\n\n").arg(cfg.workingDirectory);
    }
    preview += QStringLiteral("Program image:\n  %1\n").arg(cfg.programImage);
    if (!cfg.symbolFile.isEmpty()) {
        preview += QStringLiteral("Symbol file:\n  %1\n").arg(cfg.symbolFile);
    }

    m_commandPreview->setText(preview);
}

// ============================================================================
// Browse buttons
// ============================================================================

void HardwareDebugPage::onBrowseGdb()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select GDB executable"));
    if (!path.isEmpty())
        m_gdbPathEdit->setText(path);
}

void HardwareDebugPage::onBrowseServer()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select GDB Server executable"));
    if (!path.isEmpty()) {
        m_serverPathEdit->setText(path);
        if (m_serverTypeCombo->currentIndex() == static_cast<int>(HardwareServerType::STLink)
            && m_stlinkCubeProgPathEdit->text().trimmed().isEmpty()) {
            HardwareDebugConfiguration probe = HardwareDebugConfiguration::defaultConfig();
            probe.serverExecutable = path;
            const QString inferred = probe.effectiveStlinkCubeProgrammerPath();
            if (!inferred.isEmpty())
                m_stlinkCubeProgPathEdit->setText(inferred);
        }
        updateCommandPreview();
    }
}

void HardwareDebugPage::onBrowseWorkingDir()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Select working directory"));
    if (!path.isEmpty())
        m_workingDirEdit->setText(path);
}

void HardwareDebugPage::onBrowseProgramImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select program image"),
        QString(),
        tr("ELF files (*.elf);;All files (*)"));
    if (!path.isEmpty())
        m_programImageEdit->setText(path);
}

void HardwareDebugPage::onBrowseSymbolFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select symbol file"),
        QString(),
        tr("ELF files (*.elf);;All files (*)"));
    if (!path.isEmpty())
        m_symbolFileEdit->setText(path);
}

void HardwareDebugPage::onBrowseCubeProgrammer()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("Select STM32CubeProgrammer directory"));
    if (!path.isEmpty())
        m_stlinkCubeProgPathEdit->setText(path);
}

// ============================================================================
// Test Configuration
// ============================================================================

void HardwareDebugPage::onTestConfig()
{
    saveCurrentToConfig();

    const int idx = m_loadedIndex;
    if (idx < 0 || idx >= m_configManager.configurations.size())
        return;

    HardwareDebugConfiguration cfg = m_configManager.configurations[idx];

    // Accept either an absolute path or a command available through PATH.
    if (cfg.serverExecutable.isEmpty()) {
        m_testStatusLabel->setText(
            QStringLiteral("<span style='color:orange'>Server executable not configured.</span>"));
        return;
    }

    QString resolvedServer = cfg.serverExecutable;
    const QFileInfo configuredFile(resolvedServer);
    if (!configuredFile.isAbsolute() && !resolvedServer.contains(QDir::separator()))
        resolvedServer = QStandardPaths::findExecutable(resolvedServer);

    const QFileInfo fi(resolvedServer);
    if (resolvedServer.isEmpty() || !fi.exists()) {
        m_testStatusLabel->setText(
            QStringLiteral("<span style='color:orange'>Server executable not found:</span><br>%1")
            .arg(cfg.serverExecutable));
        return;
    }
    if (!fi.isExecutable()) {
        m_testStatusLabel->setText(
            QStringLiteral("<span style='color:orange'>Server executable is not executable:</span><br>%1")
            .arg(cfg.serverExecutable));
        return;
    }

    cfg.serverExecutable = resolvedServer;
    // The test starts only the GDB server. Firmware and debugger validation
    // belongs to the full session launch, not to this connectivity check.
    cfg.gdbExecutable = QStringLiteral("test-only");
    cfg.loadImage = false;
    cfg.loadSymbols = false;
    cfg.programImage.clear();
    cfg.symbolFile.clear();

    m_testStatusLabel->setText(
        tr("Starting %1 and waiting for %2:%3...")
        .arg(fi.fileName(), cfg.host).arg(cfg.port));
    m_testBtn->setEnabled(false);

    // Reset any previous test server
    if (m_testServer) {
        m_testServer->disconnect();
        m_testServer->kill();
        m_testServer.reset();
    }

    // Create a temporary server process for testing
    m_testServer = std::make_unique<GdbServerProcess>();

    connect(m_testServer.get(), &GdbServerProcess::ready, this, [this]() {
        m_testStatusLabel->setText(
            QStringLiteral("<span style='color:green'>Server is ready! Connection OK.</span>"));
        // Keep the wrapper alive until the child has actually exited.
        if (m_testServer)
            m_testServer->stop();
    });

    connect(m_testServer.get(), &GdbServerProcess::errorOccurred, this, [this](const QString& msg) {
        m_testStatusLabel->setText(
            QStringLiteral("<span style='color:red'>%1</span>").arg(msg));
        if (m_testServer && m_testServer->state() == GdbServerState::Failed) {
            QTimer::singleShot(0, this, [this]() {
                m_testBtn->setEnabled(true);
                m_testServer.reset();
            });
        }
    });

    connect(m_testServer.get(), &GdbServerProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (m_testServer && status == QProcess::CrashExit) {
            m_testStatusLabel->setText(
                QStringLiteral("<span style='color:orange'>Server process exited unexpectedly (code %1)</span>")
                .arg(exitCode));
        }
        QTimer::singleShot(0, this, [this]() {
            m_testBtn->setEnabled(true);
            m_testServer.reset();
        });
    });

    m_testServer->start(cfg);
}
