cmake_minimum_required(VERSION 3.20)
project(url_shortener LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

set(COMMON_SOURCES
    src/config.cpp
    src/database.cpp
    src/url_repository.cpp
    src/rate_limiter.cpp
    src/url_service.cpp
    src/http_server.cpp
)

add_executable(url_shortener src/main.cpp ${COMMON_SOURCES})
target_include_directories(url_shortener PRIVATE include)
target_link_libraries(url_shortener PRIVATE SQLite::SQLite3 OpenSSL::Crypto Threads::Threads)
target_compile_options(url_shortener PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)

enable_testing()
add_executable(unit_tests tests/test_main.cpp ${COMMON_SOURCES})
target_include_directories(unit_tests PRIVATE include)
target_link_libraries(unit_tests PRIVATE SQLite::SQLite3 OpenSSL::Crypto Threads::Threads)
target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
add_test(NAME unit_tests COMMAND unit_tests)
