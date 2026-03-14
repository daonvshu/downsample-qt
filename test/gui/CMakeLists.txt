set(DOWNSAMPLE_QT_GUI_TEST_SOURCES
    main.cpp
    dataplotwindow.h
    dataplotwindow.cpp
    qcustomplot.h
    qcustomplot.cpp
)

find_package(Threads REQUIRED)

add_executable(downsample_qt_gui_test
    ${DOWNSAMPLE_QT_GUI_TEST_SOURCES}
)

target_link_libraries(downsample_qt_gui_test
    PRIVATE
        downsample_qt_lib
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::PrintSupport
        Threads::Threads
)

target_compile_features(downsample_qt_gui_test PRIVATE cxx_std_17)
