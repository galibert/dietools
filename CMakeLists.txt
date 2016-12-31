cmake_minimum_required(VERSION 3.4)
project(dietools)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-strict-overflow -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-strict-overflow -Wno-unused-parameter")

find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})
find_package(lua REQUIRED)
include_directories(${LUA_INCLUDE_DIR})
find_package(zlib REQUIRED)
include_directories(${ZLIB_INCLUDE_DIRS})
find_package(Qt5Gui)
find_package(Qt5Widgets)
# This doesn't work! Why?
# find_package(Qt REQUIRED COMPONENTS Qt5Gui Qt5Widgets)

include_directories("${PROJECT_SOURCE_DIR}/libdie")
add_subdirectory(generate-bitmask-images)
add_subdirectory(generate-circuit)
add_subdirectory(libdie)
add_subdirectory(logx)
add_subdirectory(mschem)
add_subdirectory(mview)
add_subdirectory(sview)

install(TARGETS generate-bitmask-images generate-circuit libdie logx mschem mview sview)
