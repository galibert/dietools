add_executable(logx logx.cc)
set_target_properties(logx PROPERTIES COMPILE_FLAGS -std=c++11)
target_link_libraries(logx die)
install(TARGETS logx RUNTIME DESTINATION bin)
