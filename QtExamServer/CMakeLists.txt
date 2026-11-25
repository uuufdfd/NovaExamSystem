cmake_minimum_required(VERSION 3.5)

project(QtExamServer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 QUIET COMPONENTS Core Network Sql)
if(Qt6_FOUND)
    set(QT_PACKAGE Qt6)
    set(CMAKE_CXX_STANDARD 17)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Network Sql)
    set(QT_PACKAGE Qt5)
    set(CMAKE_CXX_STANDARD 11)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(QtExamServer
    src/main.cpp
    src/database.h
    src/database.cpp
    src/riskscoremanager.h
    src/riskscoremanager.cpp
    src/examserver.h
    src/examserver.cpp
)

target_link_libraries(QtExamServer PRIVATE ${QT_PACKAGE}::Core ${QT_PACKAGE}::Network ${QT_PACKAGE}::Sql)

if(WIN32)
    set_target_properties(QtExamServer PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
