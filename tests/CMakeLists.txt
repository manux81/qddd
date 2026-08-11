set(CMAKE_AUTOMOC ON)

add_executable(mi_stream_buffer_tests
    MiStreamBufferTests.cpp
    ../src/MiStreamBuffer.cpp
    ../src/MiStreamBuffer.h
)
target_include_directories(mi_stream_buffer_tests PRIVATE ../src)
target_link_libraries(mi_stream_buffer_tests PRIVATE Qt5::Core)
add_test(NAME mi_stream_buffer_tests COMMAND mi_stream_buffer_tests)

# This characterization test drives a POSIX shell script pretending to be MDB.
# Keep it on Unix; the runtime graph test below remains portable across all
# release platforms.
if(UNIX)
	add_executable(debug_session_mi_tests
		DebugSessionMiTests.cpp
		../src/DebugSession.cpp
		../src/DebugSession.h
		../src/MiStreamBuffer.cpp
		../src/MiStreamBuffer.h
	)
	target_include_directories(debug_session_mi_tests PRIVATE ../src)
	target_link_libraries(debug_session_mi_tests PRIVATE Qt5::Core)
	add_test(NAME debug_session_mi_tests COMMAND debug_session_mi_tests)

    add_executable(mdb_process_characterization
        MdbProcessCharacterization.cpp
        ../src/MdbProcess.cpp
        ../src/MdbProcess.h
        ../src/HardwareServerConfig.cpp
        ../src/HardwareServerConfig.h
    )

    target_include_directories(mdb_process_characterization PRIVATE ../src)
    target_link_libraries(mdb_process_characterization PRIVATE Qt5::Core)
    add_test(NAME mdb_process_characterization COMMAND mdb_process_characterization)
endif()

add_executable(runtime_object_graph_tests
    RuntimeObjectGraphTests.cpp
    ../src/RuntimeObjectGraph.cpp
    ../src/RuntimeObjectGraph.h
)
target_include_directories(runtime_object_graph_tests PRIVATE ../src)
target_link_libraries(runtime_object_graph_tests PRIVATE Qt5::Core)
add_test(NAME runtime_object_graph_tests COMMAND runtime_object_graph_tests)

add_executable(runtime_graph_layout_tests
	RuntimeGraphLayoutTests.cpp
	../src/RuntimeGraphLayout.cpp
	../src/RuntimeGraphLayout.h
)
target_include_directories(runtime_graph_layout_tests PRIVATE ../src)
target_link_libraries(runtime_graph_layout_tests PRIVATE Qt5::Core)
add_test(NAME runtime_graph_layout_tests COMMAND runtime_graph_layout_tests)
