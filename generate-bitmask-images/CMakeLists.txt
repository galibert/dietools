add_executable(generate-bitmask-images generate-bitmask-images.cc nanosvg.c nanosvgrast.c)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(generate-bitmask-images die)
install(TARGETS generate-bitmask-images RUNTIME DESTINATION bin)
