cmake_minimum_required(VERSION 3.15)
project(wtssh LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WTSSH_CREDENTIAL_BACKEND "auto" CACHE STRING
    "Credential backend: auto, native, windows, keychain, libsecret, or none")
set_property(CACHE WTSSH_CREDENTIAL_BACKEND PROPERTY STRINGS
    auto native windows keychain libsecret none)
string(TOLOWER "${WTSSH_CREDENTIAL_BACKEND}" WTSSH_CREDENTIAL_BACKEND_NORMALIZED)

if(WTSSH_CREDENTIAL_BACKEND_NORMALIZED STREQUAL "native")
    set(WTSSH_CREDENTIAL_BACKEND_NORMALIZED "auto")
endif()

if(WTSSH_CREDENTIAL_BACKEND_NORMALIZED STREQUAL "auto")
    if(WIN32)
        set(WTSSH_CREDENTIAL_BACKEND_RESOLVED "windows")
    elseif(APPLE)
        set(WTSSH_CREDENTIAL_BACKEND_RESOLVED "keychain")
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBSECRET QUIET IMPORTED_TARGET libsecret-1)
        endif()
        if(TARGET PkgConfig::LIBSECRET)
            set(WTSSH_CREDENTIAL_BACKEND_RESOLVED "libsecret")
        else()
            set(WTSSH_CREDENTIAL_BACKEND_RESOLVED "none")
            message(WARNING
                "libsecret-1 was not found; password storage will be disabled. "
                "Install libsecret development files or set WTSSH_CREDENTIAL_BACKEND=none explicitly.")
        endif()
    endif()
else()
    set(WTSSH_CREDENTIAL_BACKEND_RESOLVED "${WTSSH_CREDENTIAL_BACKEND_NORMALIZED}")
endif()

if(WTSSH_CREDENTIAL_BACKEND_RESOLVED STREQUAL "windows")
    if(NOT WIN32)
        message(FATAL_ERROR "The windows credential backend requires Windows.")
    endif()
    set(WTSSH_CREDENTIAL_SOURCE src/credential_store_windows.cpp)
elseif(WTSSH_CREDENTIAL_BACKEND_RESOLVED STREQUAL "keychain")
    if(NOT APPLE)
        message(FATAL_ERROR "The keychain credential backend requires macOS.")
    endif()
    set(WTSSH_CREDENTIAL_SOURCE src/credential_store_macos.cpp)
elseif(WTSSH_CREDENTIAL_BACKEND_RESOLVED STREQUAL "libsecret")
    if(WIN32 OR APPLE)
        message(FATAL_ERROR "The libsecret credential backend is supported on Linux/Unix builds.")
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
    set(WTSSH_CREDENTIAL_SOURCE src/credential_store_libsecret.cpp)
elseif(WTSSH_CREDENTIAL_BACKEND_RESOLVED STREQUAL "none")
    set(WTSSH_CREDENTIAL_SOURCE src/credential_store_none.cpp)
else()
    message(FATAL_ERROR "Unknown WTSSH_CREDENTIAL_BACKEND: ${WTSSH_CREDENTIAL_BACKEND}")
endif()

message(STATUS "WT SSH credential backend: ${WTSSH_CREDENTIAL_BACKEND_RESOLVED}")

add_executable(wtssh
    wt_ssh_tui.cpp
    src/credential_store.cpp
    ${WTSSH_CREDENTIAL_SOURCE}
)
add_executable(scpm
    scp_tui.cpp
    src/scp_paths.cpp
    src/credential_store.cpp
    ${WTSSH_CREDENTIAL_SOURCE}
)
target_include_directories(wtssh PRIVATE src)
target_include_directories(scpm PRIVATE src)

if(WTSSH_CREDENTIAL_BACKEND_RESOLVED STREQUAL "windows")
    target_link_libraries(wtssh PRIVATE Advapi32)
    target_link_libraries(scpm PRIVATE Advapi32)
elseif(WTSSH_CREDENTIAL_BACKEND_RESOLVED STREQUAL "keychain")
    target_link_libraries(wtssh PRIVATE "-framework Security" "-framework CoreFoundation")
    target_link_libraries(scpm PRIVATE "-framework Security" "-framework CoreFoundation")
elseif(WTSSH_CREDENTIAL_BACKEND_RESOLVED STREQUAL "libsecret")
    target_link_libraries(wtssh PRIVATE PkgConfig::LIBSECRET)
    target_link_libraries(scpm PRIVATE PkgConfig::LIBSECRET)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(scpm_paths_test tests/scp_paths_test.cpp src/scp_paths.cpp)
    target_include_directories(scpm_paths_test PRIVATE src)
    add_test(NAME scpm_paths COMMAND scpm_paths_test)

    add_executable(wtssh_fake_ssh tests/fake_ssh.cpp)
    if(WIN32)
        add_test(
            NAME wtssh_integration
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_integration.ps1
                -Wtssh $<TARGET_FILE:wtssh>
                -FakeSsh $<TARGET_FILE:wtssh_fake_ssh>
                -BuildDir ${CMAKE_CURRENT_BINARY_DIR}
        )
        add_test(
            NAME scpm_integration
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_scp_integration.ps1
                -Scpm $<TARGET_FILE:scpm>
                -FakeScp $<TARGET_FILE:wtssh_fake_ssh>
                -BuildDir ${CMAKE_CURRENT_BINARY_DIR}
        )
    endif()
endif()
