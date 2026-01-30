cmake_minimum_required(VERSION 3.10)
project(ghip)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_executable(ghip
    src/main.cpp
    src/ip-fetcher.cpp
    src/ip-tester.cpp
    src/hosts-manager.cpp
    src/terminal-ui.cpp
)

target_link_libraries(ghip
    ${CURL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

target_include_directories(ghip PRIVATE src)
