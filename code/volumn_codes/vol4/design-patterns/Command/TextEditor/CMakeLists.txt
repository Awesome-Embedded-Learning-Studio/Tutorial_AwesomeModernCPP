cmake_minimum_required(VERSION 3.10)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(TextEditor VERSION 0.1.0 LANGUAGES C CXX)
add_executable(TextEditor TextEditor.h TextEditorMain.cpp)
