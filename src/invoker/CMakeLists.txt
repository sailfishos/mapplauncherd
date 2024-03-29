set(COMMON "${CMAKE_HOME_DIRECTORY}/src/common")

# Find dbus
include(FindPkgConfig)
pkg_check_modules(DBUS dbus-1 REQUIRED)
pkg_check_modules(GLIB glib-2.0 REQUIRED)

# Set sources
set(SRC invokelib.c invoker.c ${COMMON}/report.c search.c ${COMMON}/sailjail.c)

# Set include dirs
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${DBUS_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS} ${COMMON})

# Set precompiler flags
add_definitions(-DPROG_NAME_INVOKER="invoker")

# Set target
add_executable(invoker ${SRC})

target_link_libraries(invoker ${DBUS_LDFLAGS} ${GLIB_LDFLAGS})

# Add install rule
install(TARGETS invoker DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
