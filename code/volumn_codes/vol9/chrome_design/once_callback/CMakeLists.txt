# once_callback: header-only interface library
add_library(once_callback INTERFACE)
target_include_directories(once_callback INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(once_callback INTERFACE cxx_std_23)
