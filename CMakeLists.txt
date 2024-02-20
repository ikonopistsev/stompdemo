cmake_minimum_required(VERSION 3.10)

project(stompdemo VERSION 1.1.6)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

option(STOMPDEMO_STATIC_LIBEVENT "use static libevent" OFF)

if (STOMPDEMO_STATIC_LIBEVENT)
    add_subdirectory(libevent)
    include_directories(libevent libevent/include ${CMAKE_CURRENT_BINARY_DIR}/libevent/include)
endif()

add_subdirectory(stomptalk)
add_subdirectory(stompconn)

add_executable(justconnect just_connect.cpp)
add_executable(justsubscribe just_subscribe.cpp)

if (STOMPDEMO_STATIC_LIBEVENT)
    target_link_libraries(justconnect PRIVATE event_core_static event_extra_static stomptalk stompconn)
    target_link_libraries(justsubscribe PRIVATE event_core_static event_extra_static stomptalk stompconn)
else()
    target_link_libraries(justconnect PRIVATE stomptalk stompconn)
    target_link_libraries(justsubscribe PRIVATE stomptalk stompconn)
endif()

if (WIN32)
    target_link_libraries(justconnect PRIVATE ws2_32)
    target_link_libraries(justsubscribe PRIVATE ws2_32)
endif()

