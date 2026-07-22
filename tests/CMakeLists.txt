set(CMAKE_AUTOMOC ON)

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
