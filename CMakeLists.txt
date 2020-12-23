cmake_minimum_required(VERSION 3.17)
project(Roar C)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

set(CMAKE_C_STANDARD 11)
set(CFLAGS "-std=c11 -Wall")

find_package(SoundIo)
if(SOUNDIO_FOUND)
    include_directories(${SOUNDIO_INCLUDE_DIR})
    set(STATUS_SOUNDIO "OK")
else()
    set(STATUS_SOUNDIO "not found")
endif()

add_executable(roar src/main.c)
set_target_properties(roar PROPERTIES
        LINKER_LANGUAGE C
        COMPILE_FLAGS ${CFLAGS})
target_link_libraries(roar
        ${SOUNDIO_LIBRARY}
        )

install(TARGETS roar DESTINATION ${CMAKE_INSTALL_BINDIR})