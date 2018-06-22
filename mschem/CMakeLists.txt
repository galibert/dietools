add_executable(mschem mschem.cc pad_info.cc globals.cc)
target_link_libraries(mschem die ${FREETYPE_LIBRARIES} ${FONTCONFIG_LIBRARIES} ${LUA_LIBRARIES} ${ZLIB_LIBRARIES})
install(TARGETS mschem RUNTIME DESTINATION bin)
