set(DOWNSAMPLE_QT_LIBRARY_SOURCES
    downsampler.cpp
)

set(DOWNSAMPLE_QT_LIBRARY_HEADERS
    spscdataringbuffer.h
    downsampler.h
)

add_library(downsample_qt_lib
    ${DOWNSAMPLE_QT_LIBRARY_SOURCES}
    ${DOWNSAMPLE_QT_LIBRARY_HEADERS}
)

target_include_directories(downsample_qt_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(downsample_qt_lib
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_features(downsample_qt_lib PUBLIC cxx_std_17)
