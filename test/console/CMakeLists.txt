find_package(Threads REQUIRED)

set(DOWNSAMPLE_QT_CONSOLE_TEST_SOURCES
    main.cpp
)

add_executable(downsample_qt_test
    ${DOWNSAMPLE_QT_CONSOLE_TEST_SOURCES}
)

target_link_libraries(downsample_qt_test
    PRIVATE
        downsample_qt_lib
        Qt${QT_VERSION_MAJOR}::Core
        Threads::Threads
)

target_compile_features(downsample_qt_test PRIVATE cxx_std_17)
