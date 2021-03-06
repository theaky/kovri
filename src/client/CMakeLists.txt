set(CLIENT_SRC
	"AddressBook.cpp"
	"Destination.cpp"
    "ClientContext.cpp"
    "Daemon.cpp"
    "I2PService.cpp"
    "Kovri.cpp"
    "util/Config.cpp"
    "util/Filesystem.cpp"
)

include_directories("." "../" "../core/")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CLIENT_SRC "DaemonLinux.cpp")
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    list(APPEND CLIENT_SRC "DaemonLinux.cpp")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND CLIENT_SRC "DaemonLinux.cpp")
elseif(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
    list(APPEND CLIENT_SRC "DaemonLinux.cpp")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND CLIENT_SRC "DaemonWin32.cpp")
    list(APPEND CLIENT_SRC "Win32Service.cpp")
endif()

if(WITH_BINARY)

    add_executable(${CLIENT_NAME} ${CLIENT_SRC})

    if(NOT MSVC) # FIXME: incremental linker file name (.ilk) collision for dll & exe
        set_target_properties(${CLIENT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

        if(WITH_STATIC)
            set_target_properties(${CLIENT_NAME} PROPERTIES LINK_FLAGS "-static" )
        endif()

    endif()

    if(WITH_HARDENING AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_target_properties(${CLIENT_NAME} PROPERTIES LINK_FLAGS "-z relro -z now")
    endif()

    # FindBoost pulls pthread for thread which is broken for static linking
    # (at least on Ubuntu 15.04)
    list(GET Boost_LIBRARIES -1 LAST_Boost_LIBRARIES)

    if(${LAST_Boost_LIBRARIES} MATCHES ".*pthread.*")
        list(REMOVE_AT Boost_LIBRARIES -1)
    endif()

    target_link_libraries(
	${CLIENT_NAME} ${API_NAME} ${CORE_NAME}
        ${DL_LIB} ${Boost_LIBRARIES} ${CRYPTO++_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT}
    )

    install(TARGETS
        ${CLIENT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    if(MSVC)
        install(FILES
            $<TARGET_PDB_FILE:${CLIENT_NAME}> DESTINATION "bin" CONFIGURATIONS DEBUG
        )
    endif()

endif()

# vim: noai:ts=4:sw=4
