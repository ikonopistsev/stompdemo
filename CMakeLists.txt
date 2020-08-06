cmake_minimum_required(VERSION 3.10)
project(stompdemo VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#set(EVENT__DISABLE_BENCHMARK ON)
#set(EVENT__DISABLE_MBEDTLS ON)
#set(EVENT__DISABLE_OPENSSL ON)
#set(EVENT__DISABLE_REGRESS ON)
#set(EVENT__LIBRARY_TYPE STATIC)
#set(EVENT__DISABLE_REGRESS ON)
#set(EVENT__DISABLE_SAMPLES ON)
#set(EVENT__DISABLE_TESTS ON)

add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)

add_subdirectory(libevent)

include_directories(stomptalk/include stompconn/include libevent/include ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/libevent/include)

add_subdirectory(stomptalk)
add_subdirectory(stompconn)

add_executable(stompdemo main.cpp)

target_link_libraries(stompdemo PRIVATE event event_core stomptalk stompconn)