cmake_minimum_required(VERSION 3.10)

project(stompdemo VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(STOMPDEMO_STATIC_LIBEVENT "add static libevent" OFF)

#set(EVENT__DISABLE_MBEDTLS ON)
#set(EVENT__DISABLE_OPENSSL ON)
#set(EVENT__DISABLE_BENCHMARK ON)
#set(EVENT__DISABLE_REGRESS ON)
#set(EVENT__DISABLE_REGRESS ON)
#set(EVENT__DISABLE_SAMPLES ON)
#set(EVENT__DISABLE_TESTS ON)

if (STOMPDEMO_STATIC_LIBEVENT)
    set(EVENT__LIBRARY_TYPE "STATIC")
    add_subdirectory(libevent)
    include_directories(libevent/include ${CMAKE_CURRENT_BINARY_DIR}/libevent/include)
endif()

add_subdirectory(stomptalk)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} stomptalk/include)

add_subdirectory(stompconn)

add_executable(stompdemo main.cpp)

target_link_libraries(stompdemo PRIVATE event stomptalk stompconn)

if (WIN32)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_link_libraries(stompdemo PRIVATE ws2_32)
endif()
