cmake_minimum_required(VERSION 3.10)
project(GHIP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_executable(GHIP
    src/main.cpp
    src/ip-fetcher.cpp
    src/ip-tester.cpp
    src/hosts-manager.cpp
    src/terminal-ui.cpp
)

target_link_libraries(GHIP
    ${CURL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

target_include_directories(GHIP PRIVATE src)
