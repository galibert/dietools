add_library(die State.cc circuit_map.cc images.cc reader.cc timing.cc circuit_info.cc fill.cc net_info.cc)
install(TARGETS die ARCHIVE DESTINATION lib)
