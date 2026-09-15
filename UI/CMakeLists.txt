cmake_minimum_required(VERSION 3.21)
project(hypr-pills LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus)
find_package(LayerShellQt REQUIRED)

qt_add_executable(hypr-pills
  src/main.cpp
  src/barwindow.cpp
  src/barwindow.h
  src/cornerbar.cpp
  src/cornerbar.h
  src/pill.cpp
  src/pill.h
  src/hyprland.cpp
  src/hyprland.h
  src/statusmonitor.cpp
  src/statusmonitor.h
  src/dropdown.cpp
  src/dropdown.h
  src/applauncher.cpp
  src/applauncher.h
  src/toast.cpp
  src/toast.h
)

target_link_libraries(hypr-pills PRIVATE
  Qt6::Widgets
  Qt6::Network
  Qt6::DBus
  LayerShellQt::Interface
)

install(TARGETS hypr-pills RUNTIME DESTINATION bin)
