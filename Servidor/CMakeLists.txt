cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The headless server targets are Linux-only")
endif()

find_package(Threads REQUIRED)

option(
    OPENWYD_SANITIZE
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)

set(OPENWYD_SERVER_CODE "${CMAKE_CURRENT_SOURCE_DIR}/Source/Code")
set(OPENWYD_SERVER_COMPAT "${CMAKE_CURRENT_SOURCE_DIR}/Source/Compat")

set(OPENWYD_COMMON_SOURCES
    "${OPENWYD_SERVER_CODE}/Basedef.cpp"
    "${OPENWYD_SERVER_CODE}/ConfigIni.cpp"
    "${OPENWYD_SERVER_CODE}/CPSock.cpp"
    "${OPENWYD_SERVER_COMPAT}/linux_main.cpp"
    "${OPENWYD_SERVER_COMPAT}/linux_win32.cpp"
    "${OPENWYD_SERVER_COMPAT}/layout_asserts.cpp"
)

file(GLOB OPENWYD_TMSRV_SOURCES CONFIGURE_DEPENDS
    "${OPENWYD_SERVER_CODE}/TMSrv/*.cpp"
)

foreach(source_name IN ITEMS
    Basedef.cpp
    CFileDB.cpp
    DialogConfigExtra.cpp
    EventTrade.cpp
    pch.cpp
    Source.cpp
)
    list(REMOVE_ITEM OPENWYD_TMSRV_SOURCES
        "${OPENWYD_SERVER_CODE}/TMSrv/${source_name}"
    )
endforeach()

set(OPENWYD_DBSRV_SOURCES
    "${OPENWYD_SERVER_CODE}/DBSrv/CFileDB.cpp"
    "${OPENWYD_SERVER_CODE}/DBSrv/CRanking.cpp"
    "${OPENWYD_SERVER_CODE}/DBSrv/CReadFiles.cpp"
    "${OPENWYD_SERVER_CODE}/DBSrv/CUser.cpp"
    "${OPENWYD_SERVER_CODE}/DBSrv/Server.cpp"
    "${OPENWYD_SERVER_CODE}/DBSrv/Sqlite_Connect.cpp"
)

function(openwyd_add_server target server_define)
    add_executable(${target} ${ARGN} ${OPENWYD_COMMON_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_definitions(${target} PRIVATE
        OPENWYD_HEADLESS=1
        ${server_define}=1
        _CRT_SECURE_NO_WARNINGS=1
        _PACKET_DEBUG=1
    )
    target_include_directories(${target} PRIVATE
        "${OPENWYD_SERVER_COMPAT}"
        "${OPENWYD_SERVER_CODE}"
        "${OPENWYD_SERVER_CODE}/sqlite3"
    )
    target_compile_options(${target} PRIVATE
        -finput-charset=UTF-8
        -fexec-charset=WINDOWS-1252
        -fsigned-char
        -Wno-deprecated-declarations
        -Wno-format
        -Wno-missing-braces
        -Wno-narrowing
        -Wno-pointer-to-int-cast
        -Wno-write-strings
    )
    target_link_libraries(${target} PRIVATE Threads::Threads)
    if(OPENWYD_SANITIZE)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
    endif()
endfunction()

openwyd_add_server(tmsrv OPENWYD_TMSRV ${OPENWYD_TMSRV_SOURCES})
openwyd_add_server(dbsrv OPENWYD_DBSRV ${OPENWYD_DBSRV_SOURCES})
