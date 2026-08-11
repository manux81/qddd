set(CMAKE_AUTOMOC ON)

# This characterization test drives a POSIX shell script pretending to be MDB.
# Keep it on Unix; the runtime graph test below remains portable across all
# release platforms.
if(UNIX)
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
