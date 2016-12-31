qt5_wrap_ui(UIS_HDRS MVMainUI.ui NetStateListUI.ui NetStateUI.ui)
add_executable(mview mview.cc MVMain.cc MVDisplay.cc NetState.cc NetStateList.cc globals.cc ${UIS_HDRS})
target_link_libraries(mview die Qt5::Gui Qt5::Widgets)
