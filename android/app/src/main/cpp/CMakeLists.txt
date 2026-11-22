cmake_minimum_required(VERSION 3.18)
project(atvui C)
# the same drawing code as the Miyoo app (src/ui.c), built for Android
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src)
add_library(atvui SHARED ${SRC}/ui.c jni.c)
target_include_directories(atvui PRIVATE ${SRC})
target_compile_definitions(atvui PRIVATE HOST=1)
target_link_libraries(atvui m)
