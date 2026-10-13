cmake_minimum_required(VERSION 3.21)
project(mpsse-blaster VERSION 2.0.0 LANGUAGES C)

if(WIN32)
    set(ftdi_platform windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ftdi_platform linux)
else()
    message(FATAL_ERROR "Only Windows and Linux x86_64 are supported.")
endif()

include(CheckCSourceCompiles)
check_c_source_compiles("
    #if !defined(__x86_64__) && !defined(_M_X64)
    #error Unsupported architecture
    #endif
    typedef char require_64_bit_pointers[sizeof(void *) == 8 ? 1 : -1];
    int main(void) { return 0; }
" TARGET_X86_64)
if(NOT TARGET_X86_64)
    message(FATAL_ERROR "An x86_64 compiler target is required. Use -A x64 with Visual Studio.")
endif()

option(BUILD_PLUGIN "Build the Quartus JTAG hardware plugin" ON)
option(BUILD_SCAN_TOOL "Build the standalone JTAG scan tool" ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(mpsse_target_defaults target)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/include")
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

set(MPSSE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/export" CACHE PATH
    "Output directory for plugin and scan tool binaries")

if(BUILD_PLUGIN OR BUILD_SCAN_TOOL)
    # The D2XX SDK is downloaded by the user and excluded from version control.
    set(FTDI_D2XX_ROOT "${PROJECT_SOURCE_DIR}/lib/${ftdi_platform}-x86_64"
        CACHE PATH "FTDI D2XX SDK directory")
    find_path(FTDI_D2XX_INCLUDE_DIR NAMES ftd2xx.h
        PATHS "${FTDI_D2XX_ROOT}" NO_DEFAULT_PATH NO_CACHE)
    if(WIN32)
        # Select a static library, not the similarly named DLL import library.
        find_file(FTDI_D2XX_LIBRARY NAMES ftd2xx.lib
            PATHS "${FTDI_D2XX_ROOT}/Static/amd64" "${FTDI_D2XX_ROOT}"
            NO_DEFAULT_PATH NO_CACHE)
    else()
        find_file(FTDI_D2XX_LIBRARY NAMES libftd2xx.a
            PATHS "${FTDI_D2XX_ROOT}" NO_DEFAULT_PATH NO_CACHE)
    endif()
    if(NOT FTDI_D2XX_INCLUDE_DIR OR NOT FTDI_D2XX_LIBRARY)
        message(FATAL_ERROR
            "D2XX headers or static library not found in ${FTDI_D2XX_ROOT}. "
            "Download the x86_64 SDK from https://ftdichip.com/drivers/d2xx-drivers/ "
            "or set FTDI_D2XX_ROOT, FTDI_D2XX_INCLUDE_DIR and FTDI_D2XX_LIBRARY.")
    endif()

    add_library(FTDI::D2XX STATIC IMPORTED)
    set_target_properties(FTDI::D2XX PROPERTIES
        IMPORTED_LOCATION "${FTDI_D2XX_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FTDI_D2XX_INCLUDE_DIR}"
    )
    if(WIN32)
        target_compile_definitions(FTDI::D2XX INTERFACE FTD2XX_STATIC)
        target_link_libraries(FTDI::D2XX INTERFACE setupapi advapi32 user32)
    else()
        set(THREADS_PREFER_PTHREAD_FLAG ON)
        find_package(Threads REQUIRED)
        target_link_libraries(FTDI::D2XX INTERFACE Threads::Threads rt)
    endif()

endif()

set(mpsse_sources
    src/mpsse_transport.c
    src/mpsse_device.c
    src/mpsse_clock.c
    src/mpsse_autotune.c
    src/mpsse_jtag.c
    src/quartus_plugin.c
)

if(BUILD_PLUGIN OR BUILD_SCAN_TOOL)
    add_library(mpsse_core OBJECT ${mpsse_sources})
    mpsse_target_defaults(mpsse_core)
    set_target_properties(mpsse_core PROPERTIES POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden)
    target_link_libraries(mpsse_core PRIVATE FTDI::D2XX)

    set(blaster_targets)
    if(BUILD_PLUGIN)
        add_library(jtag_hw_mpsse SHARED)
        list(APPEND blaster_targets jtag_hw_mpsse)
        if(NOT WIN32)
            target_link_options(jtag_hw_mpsse PRIVATE
                "LINKER:--exclude-libs,ALL" "LINKER:-z,defs")
        endif()
    endif()
    if(BUILD_SCAN_TOOL)
        add_executable(mpsse_scan tool/mpsse_scan.c)
        list(APPEND blaster_targets mpsse_scan)
    endif()
    foreach(target IN LISTS blaster_targets)
        mpsse_target_defaults(${target})
        target_link_libraries(${target} PRIVATE mpsse_core FTDI::D2XX)
        foreach(kind LIBRARY RUNTIME ARCHIVE)
            set_target_properties(${target} PROPERTIES
                ${kind}_OUTPUT_DIRECTORY "$<1:${MPSSE_OUTPUT_DIRECTORY}>")
        endforeach()
    endforeach()
endif()

include(GNUInstallDirs)
if(BUILD_PLUGIN)
    install(TARGETS jtag_hw_mpsse
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
if(BUILD_SCAN_TOOL)
    install(TARGETS mpsse_scan RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(BUILD_PLUGIN AND NOT WIN32)
    install(FILES 99-mpsse-blaster.rules
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mpsse-blaster)
endif()
