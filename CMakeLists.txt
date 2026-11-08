cmake_minimum_required(VERSION 3.21)
project(gyvm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets DBus)
qt_standard_project_setup()

# 图形界面（普通用户运行）
qt_add_executable(gyvm
    src/main.cpp
    src/backend.cpp
    src/mainwindow.cpp
)
target_link_libraries(gyvm PRIVATE Qt6::Widgets Qt6::DBus)

# 特权守护进程（root 运行；替 GUI 拉起 crosvm）
qt_add_executable(gyvmd
    src/gyvmd.cpp
    src/daemon.h
)
target_link_libraries(gyvmd PRIVATE Qt6::Core Qt6::DBus)

# 命令行
qt_add_executable(gyvmctl
    src/gyvmctl.cpp
    src/backend.cpp
)
target_link_libraries(gyvmctl PRIVATE Qt6::Core Qt6::DBus)

install(TARGETS gyvm gyvmd gyvmctl RUNTIME DESTINATION bin)
