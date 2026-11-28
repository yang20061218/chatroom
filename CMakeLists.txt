cmake_minimum_required(VERSION 3.15)

project(chatroom C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/app)

find_package(Threads REQUIRED)

set(common_sources
    src/net.c
    src/protocol.c
)

add_executable(server
    src/server.c
    ${common_sources}
)

add_executable(client
    src/client.c
    ${common_sources}
)

target_include_directories(server PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(client PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_compile_options(server PRIVATE -Wall -Wextra)
target_compile_options(client PRIVATE -Wall -Wextra)

target_link_libraries(server PRIVATE Threads::Threads)
target_link_libraries(client PRIVATE Threads::Threads)
