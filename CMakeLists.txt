cmake_minimum_required(VERSION 3.16)

# Build a single C++ server executable from the current src/ tree.
project(webserver LANGUAGES CXX)

# Match the language features used in the codebase.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Place the final executable under build/bin/.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Explicitly list translation units that make up the server.
# Header-only utilities such as auth_utils.h do not need to be listed here.
set(WEBSERVER_SOURCES
    src/main.cpp
    src/cache/redis_client.cpp
    src/dao/resource_dao.cpp
    src/dao/user_dao.cpp
    src/db/connection_pool.cpp
    src/network/http_conn.cpp
    src/service/resource_service.cpp
    src/service/user_service.cpp
    src/utils/logger.cpp
)

add_executable(${PROJECT_NAME} ${WEBSERVER_SOURCES})

# Allow includes like "thread/locker.h" by treating src/ as an include root.
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

# Core dependencies used by the project:
# - Threads: thread pool, mutexes, semaphores
# - OpenSSL: SHA256 hashing in auth_utils.h
# - nlohmann_json: JSON parsing/serialization in HTTP and service layers
# - PkgConfig: used here to discover hiredis and MySQL/MariaDB client libs
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)

# Redis client library used by src/cache/redis_client.cpp.
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)

# The database code uses the MySQL client library via <mysql/mysql.h>.
# We look up the common pkg-config names used by MySQL development packages.
pkg_check_modules(MYSQLCLIENT IMPORTED_TARGET mysqlclient)
if(NOT MYSQLCLIENT_FOUND)
    pkg_check_modules(MYSQLCLIENT IMPORTED_TARGET libmysqlclient)
endif()

# Fail early with a clear message if the MySQL development package is missing.
if(NOT MYSQLCLIENT_FOUND)
    message(FATAL_ERROR
        "Could not find the MySQL client library. "
        "Install libmysqlclient-dev."
    )
endif()

# Link the executable against all external libraries required by the current code.
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Threads::Threads
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        PkgConfig::HIREDIS
        PkgConfig::MYSQLCLIENT
)

enable_testing()

add_executable(unit_tests
    tests/unit_tests.cpp
)

target_include_directories(unit_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(unit_tests
    PRIVATE
        OpenSSL::Crypto
)

add_test(NAME unit_tests COMMAND unit_tests)
