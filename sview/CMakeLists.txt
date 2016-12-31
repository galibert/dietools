qt5_wrap_ui(UIS_HDRS SVMainUI.ui NetStateListUI.ui NetStateUI.ui)
add_executable(sview sview.cc SVMain.cc SVDisplay.cc NetState.cc NetStateList.cc state.cc ${UIS_HDRS})
target_link_libraries(sview die ${FREETYPE_LIBRARIES} Qt5::Gui Qt5::Widgets)
install(TARGETS sview RUNTIME DESTINATION bin)
