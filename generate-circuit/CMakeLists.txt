add_executable(generate-circuit generate-circuit.cc)
target_link_libraries(generate-circuit die)
install(TARGETS generate-circuit RUNTIME DESTINATION bin)
