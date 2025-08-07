configure_file(config.h.in config.h)

file(GLOB_RECURSE fdt-viewer-headers *.hpp)
file(GLOB_RECURSE fdt-viewer-sources *.cpp)
file(GLOB_RECURSE fdt-viewer-uis *.ui)
list(REMOVE_ITEM fdt-viewer-sources "main.cpp")

add_library(fdtviewer SHARED
    ${fdt-viewer-headers}
    ${fdt-viewer-sources}
    ${fdt-viewer-uis}
)
target_include_directories(fdtviewer PUBLIC include/)

target_link_libraries(fdtviewer Qt6::Widgets Qt6::Core QScintilla QHexView)
install(TARGETS fdtviewer LIBRARY DESTINATION lib)
install(DIRECTORY ./include/ DESTINATION include COMPONENT fdtviewer)

add_executable(fdt-viewer
    main.cpp
    ../resources.qrc
)
target_link_libraries(fdt-viewer PUBLIC Qt6::Widgets fdtviewer)

if (UNIX)
    set_target_properties(fdt-viewer PROPERTIES INSTALL_RPATH "$ORIGIN/../lib64;$ORIGIN/../lib")
endif (UNIX)

install(TARGETS fdt-viewer RUNTIME DESTINATION bin)


