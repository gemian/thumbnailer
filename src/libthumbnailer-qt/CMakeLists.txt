set(dbusinterface_xml "${CMAKE_SOURCE_DIR}/src/service/dbusinterface.xml")
set_source_files_properties(${dbusinterface_xml} PROPERTIES
  CLASSNAME ThumbnailerInterface
  INCLUDE ${CMAKE_SOURCE_DIR}/include/service/client_config.h)
qt5_add_dbus_interface(interface_files ${dbusinterface_xml} thumbnailerinterface)

add_library(${LIBTHUMBNAILER_QT} SHARED
  libthumbnailer-qt.cpp
  ${CMAKE_SOURCE_DIR}/src/service/client_config.cpp
  ${CMAKE_SOURCE_DIR}/include/unity/thumbnailer/qt/thumbnailer-qt.h
  ${interface_files}
)

include_directories(${CMAKE_BINARY_DIR}/src)

set_target_properties(${LIBTHUMBNAILER_QT} PROPERTIES
    VERSION "${LIBTHUMBNAILER_QT_SO_VERSION_MAJOR}.${LIBTHUMBNAILER_QT_SO_VERSION_MINOR}.${LIBTHUMBNAILER_QT_SO_VERSION_MICRO}"
    SOVERSION ${LIBTHUMBNAILER_QT_SO_VERSION}
)

set(symbol_map "${CMAKE_SOURCE_DIR}/src/libthumbnailer-qt/libthumbnailer-qt.map")
set_target_properties(${LIBTHUMBNAILER_QT} PROPERTIES
                      LINK_FLAGS "${ldflags} -Wl,--version-script,${symbol_map} ")
set_target_properties(${LIBTHUMBNAILER_QT} PROPERTIES LINK_DEPENDS ${symbol_map})

qt5_use_modules(${LIBTHUMBNAILER_QT} DBus Gui)
target_link_libraries(${LIBTHUMBNAILER_QT} thumbnailer-static Qt5::DBus Qt5::Gui)
set_target_properties(${LIBTHUMBNAILER_QT} PROPERTIES AUTOMOC TRUE)
add_dependencies(${LIBTHUMBNAILER_QT} thumbnailer-service)

install(
  TARGETS thumbnailer-qt
  LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}
)

# Set up package config.
configure_file(lib${LIBTHUMBNAILER_QT}.pc.in lib${LIBTHUMBNAILER_QT}.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lib${LIBTHUMBNAILER_QT}.pc DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/pkgconfig)
