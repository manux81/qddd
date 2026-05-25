cmake_minimum_required(VERSION 3.10)
project(ddd_qt5_full LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Gui Network)

add_subdirectory(src)
