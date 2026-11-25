cmake_minimum_required(VERSION 3.16)

project(QtExamSystem VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Xml Multimedia)

set(QT_EXAM_SYSTEM_SOURCES
    src/main.cpp
    src/accountstore.h
    src/accountstore.cpp
    src/apiclient.h
    src/apiclient.cpp
    src/aiproctormonitor.h
    src/aiproctormonitor.cpp
    src/logindialog.h
    src/logindialog.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/excelimporter.h
    src/excelimporter.cpp
    resources.qrc
)

if(WIN32)
    list(APPEND QT_EXAM_SYSTEM_SOURCES app.rc)
endif()

qt_add_executable(QtExamSystem
    ${QT_EXAM_SYSTEM_SOURCES}
)

# 保留题库页既有资源别名，与 resources.qrc 中的品牌资源使用相同原创图标。
qt_add_resources(QtExamSystem question_branding
    PREFIX "/question-branding"
    BASE "resources"
    FILES "resources/app_icon.png"
)

target_link_libraries(QtExamSystem PRIVATE Qt6::Widgets Qt6::Network Qt6::Xml Qt6::Multimedia)

if(UNIX AND NOT APPLE)
    # 麒麟/Linux 必须以 PIE 链接：非 PIE 时程序里取过地址的 Qt 信号（如 &QObject::destroyed）会生成 canonical PLT，
    # Qt 库内部的新式 connect 随之失败（signal not found）；QDialogButtonBox 不知道旧按钮已删除，
    # QFileDialog 构造时访问已释放的按钮而崩溃（教师端点击“导入题库”闪退）。
    include(CheckPIESupported)
    check_pie_supported()
    set_target_properties(QtExamSystem PROPERTIES POSITION_INDEPENDENT_CODE ON)

    get_target_property(QT_CORE_LIBRARY Qt6::Core IMPORTED_LOCATION)
    if(NOT QT_CORE_LIBRARY)
        get_target_property(QT_CORE_LIBRARY Qt6::Core IMPORTED_LOCATION_RELEASE)
    endif()
    if(QT_CORE_LIBRARY)
        get_filename_component(QT_RUNTIME_LIBRARY_DIR "${QT_CORE_LIBRARY}" DIRECTORY)
        set_target_properties(QtExamSystem PROPERTIES
            BUILD_RPATH "${QT_RUNTIME_LIBRARY_DIR}"
            INSTALL_RPATH "${QT_RUNTIME_LIBRARY_DIR}"
        )
    endif()
endif()

if(WIN32)
    set_target_properties(QtExamSystem PROPERTIES WIN32_EXECUTABLE TRUE)
    get_filename_component(QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${QT_BIN_DIR}"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET QtExamSystem POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:QtExamSystem>"
            COMMENT "Deploying Qt runtime libraries"
        )
    endif()
endif()

add_custom_command(TARGET QtExamSystem POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/tools"
        "$<TARGET_FILE_DIR:QtExamSystem>/tools"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:QtExamSystem>/data"
)
