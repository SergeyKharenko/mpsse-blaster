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

option(BUILD_SCAN_TOOL "Build the standalone JTAG scan tool" ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W3)
else()
    add_compile_options(-Wall)
endif()

# Keep all configurations' binaries in export/ without an extra Debug/Release directory.
foreach(kind LIBRARY RUNTIME ARCHIVE)
    set(CMAKE_${kind}_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/export>")
endforeach()

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

add_library(jtag_hw_mpsse SHARED JTAG-MPSSE-Blaster.c JTAG-MPSSE-Blaster.h)
set(blaster_targets jtag_hw_mpsse)
if(BUILD_SCAN_TOOL)
    add_executable(jtag_scan_tool tool/JTAG-Scan-Tool.c JTAG-MPSSE-Blaster.c)
    target_compile_definitions(jtag_scan_tool PRIVATE _TEST_TOOL)
    list(APPEND blaster_targets jtag_scan_tool)
endif()
foreach(target IN LISTS blaster_targets)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(${target} PRIVATE FTDI::D2XX)
endforeach()
if(NOT WIN32)
    # Hide embedded D2XX/libusb symbols and reject unresolved plugin symbols.
    target_link_options(jtag_hw_mpsse PRIVATE "LINKER:--exclude-libs,ALL" "LINKER:-z,defs")
endif()

include(GNUInstallDirs)
install(TARGETS jtag_hw_mpsse
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(BUILD_SCAN_TOOL)
    install(TARGETS jtag_scan_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(NOT WIN32)
    install(FILES 99-mpsse-blaster.rules
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mpsse-blaster)
endif()
