add_executable(mschem mschem.cc pad_info.cc globals.cc)
target_link_libraries(mschem die ${FREETYPE_LIBRARIES} ${LUA_LIBRARIES} ${ZLIB_LIBRARIES})
