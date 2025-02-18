set(LAUNCHER "${CMAKE_HOME_DIRECTORY}/src/launcherlib")
set(COMMON "${CMAKE_HOME_DIRECTORY}/src/common")

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${COMMON} ${LAUNCHER})

# Hide all symbols except the ones explicitly exported in the code (like main())
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")

# Set sources
set(SRC booster-generic.cpp)

# Set executable
add_executable(booster-generic ${SRC} ${MOC_SRC})

target_link_libraries(booster-generic applauncherd)

# Add install rule
install(TARGETS booster-generic DESTINATION ${CMAKE_INSTALL_FULL_LIBEXECDIR}/mapplauncherd/)

if(INSTALL_SYSTEMD_UNITS)
	install(FILES booster-generic.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user/)
	install(FILES booster-generic@.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user/)
endif()
