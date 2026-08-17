set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(qddd WIN32 MACOSX_BUNDLE
    main.cpp
    MainWindow.cpp
    MainWindow.h
    DebugSession.cpp
    DebugSession.h
	MiStreamBuffer.cpp
	MiStreamBuffer.h
    RuntimeObjectGraph.cpp
    RuntimeObjectGraph.h
	RuntimeGraphLayout.cpp
	RuntimeGraphLayout.h
	OrthogonalEdgeRouter.cpp
	OrthogonalEdgeRouter.h
    SourceEditor.cpp
    SourceEditor.h
    SourceBrowserDialog.cpp
    SourceBrowserDialog.h
    CodeHighlighter.cpp
    CodeHighlighter.h
    VariablesView.cpp
    VariablesView.h
    StackView.cpp
    StackView.h
    GraphicalVariablesView.cpp
    GraphicalVariablesView.h
    ConsoleWidget.cpp
    ConsoleWidget.h
    DisassemblyView.cpp
    DisassemblyView.h
    BreakpointsView.cpp
    BreakpointsView.h
    BreakpointEditorDialog.cpp
    BreakpointEditorDialog.h
    QDDDSplash.cpp
    QDDDSplash.h
    SettingsDialog.cpp
    SettingsDialog.h
    ToggleSwitch.cpp
    ToggleSwitch.h
    DebugAssistantDock.cpp
    DebugAssistantDock.h

    HardwareServerConfig.cpp
    HardwareServerConfig.h
    GdbServerProcess.cpp
    GdbServerProcess.h
    HardwareDebugSession.cpp
    HardwareDebugSession.h
    MdbProcess.cpp
    MdbProcess.h
    HardwareDebugPage.cpp
    HardwareDebugPage.h
    MockGdbServer.cpp
    MockGdbServer.h

    resources.qrc
)

set_target_properties(qddd PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "qddd"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.manux81.qddd"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

target_link_libraries(qddd
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Network
)
