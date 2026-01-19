set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(qddd
    main.cpp
    MainWindow.cpp
    MainWindow.h
    DebugSession.cpp
    DebugSession.h
    SourceEditor.cpp
    SourceEditor.h
    VariablesView.cpp
    VariablesView.h
    StackView.cpp
    StackView.h
    GraphicalVariablesView.cpp
    GraphicalVariablesView.h
    ConsoleWidget.cpp
    ConsoleWidget.h
    BreakpointsView.cpp
    BreakpointsView.h
    QDDDSplash.cpp
    QDDDSplash.h

    resources.qrc
)

target_link_libraries(qddd
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
)
