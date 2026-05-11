cmake_minimum_required(VERSION 3.10)
project(PCAParserEducationalProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SQLite3_INCLUDE_DIR "C:/msys64/mingw64/include")
set(SQLite3_LIBRARY "C:/msys64/mingw64/lib/libsqlite3.dll.a")

find_package(SQLite3 REQUIRED)

add_executable(PCAParserEducationalProject main.cpp)

target_link_libraries(PCAParserEducationalProject PRIVATE ${SQLite3_LIBRARY})