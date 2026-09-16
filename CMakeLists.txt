cmake_minimum_required(VERSION 3.20)
project(BeebView VERSION 0.5.88 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(VIEWBBC_BUILD_TESTS "Build ViewBBC tests" ON)
option(VIEWBBC_BUILD_NCURSES "Build ncurses terminal frontend" ON)
option(VIEWBBC_BUILD_SDL3 "Build SDL3 graphical frontend" ON)

if(WIN32)
    # The terminal frontend is POSIX/ncurses-specific; the Windows port targets SDL3.
    set(VIEWBBC_BUILD_NCURSES OFF CACHE BOOL "Build ncurses terminal frontend" FORCE)
endif()
set(VIEWBBC_SDL3_SOURCE "" CACHE PATH "Path to an SDL3 source tree containing CMakeLists.txt")

add_library(viewbbc_core
    src/core/config.c
    src/core/screen.c
    src/core/ruler.c
    src/core/markers.c
    src/core/blocks.c
    src/core/formatter.c
    src/core/document.c
    src/core/commands.c
    src/core/startup.c
    src/core/console.c
    src/core/help.c
    src/core/output.c
    src/core/print.c
    src/core/atomic_file.c
    src/core/filesystem.c
    src/core/file_io.c
    src/core/dfs.c
    src/core/editor_commands.c
    src/core/view_commands.c
    src/core/editor_command_line.c
    src/core/editor_render.c
    src/core/editor.c
    src/core/editor_mouse.c
)

target_include_directories(viewbbc_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(viewbbc_core PRIVATE -Wall -Wextra -Wpedantic)

# Release hardening on GCC/Clang-like Unix toolchains. Keep this scoped to
# BeebView rather than forcing flags on the bundled SDL3 project.
if(UNIX AND NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(viewbbc_core PRIVATE $<$<CONFIG:Release>:-fstack-protector-strong;-D_FORTIFY_SOURCE=3>)
    add_link_options($<$<CONFIG:Release>:-Wl,-z,relro,-z,now>)
endif()

if(VIEWBBC_BUILD_SDL3)
    # Preferred for Mint/older Ubuntu: build a local SDL3 source tree as part of
    # ViewBBC. This does not install SDL3 system-wide.
    if(VIEWBBC_SDL3_SOURCE)
        get_filename_component(VIEWBBC_SDL3_SOURCE_ABS
            "${VIEWBBC_SDL3_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

        if(NOT EXISTS "${VIEWBBC_SDL3_SOURCE_ABS}/CMakeLists.txt")
            message(FATAL_ERROR
                "VIEWBBC_SDL3_SOURCE does not point to an SDL3 source tree.\n"
                "Expected to find: ${VIEWBBC_SDL3_SOURCE_ABS}/CMakeLists.txt"
            )
        endif()

        message(STATUS "ViewBBC SDL3: building local source tree: ${VIEWBBC_SDL3_SOURCE_ABS}")

        # Keep the embedded SDL build lean. These are SDL's normal cache options;
        # setting them before add_subdirectory avoids building SDL examples/tests.
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

        add_subdirectory(
            "${VIEWBBC_SDL3_SOURCE_ABS}"
            "${CMAKE_BINARY_DIR}/_deps/sdl3-build"
            EXCLUDE_FROM_ALL
        )
    else()
        # Useful on distros which do package SDL3.
        find_package(SDL3 CONFIG QUIET)
        if(SDL3_FOUND)
            message(STATUS "ViewBBC SDL3: using system package")
        else()
            message(FATAL_ERROR
                "SDL3 was not found.\n"
                "On Linux Mint, point ViewBBC at the SDL3 source you downloaded, e.g.:\n"
                "  cmake -S . -B build -DVIEWBBC_SDL3_SOURCE=/path/to/SDL3-3.4.16\n"
                "or use the supplied ./build.sh.\n"
                "For a deliberate terminal-only build use -DVIEWBBC_BUILD_SDL3=OFF."
            )
        endif()
    endif()

    if(NOT TARGET SDL3::SDL3)
        message(FATAL_ERROR
            "SDL3 was configured, but the SDL3::SDL3 CMake target was not created.\n"
            "Check that VIEWBBC_SDL3_SOURCE points at the root of an SDL3 source release."
        )
    endif()

    message(STATUS "BeebView graphical frontend: SDL3 (primary target: BeebView)")
    add_executable(BeebView
        src/main_sdl3.c
        src/frontend/sdl3/sdl3_frontend.c
        src/frontend/sdl3/sdl3_clipboard.c
        src/frontend/sdl3/config_screen.c
        src/frontend/sdl3/bitmap_font.c
    )
    target_include_directories(BeebView PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(BeebView PRIVATE viewbbc_core SDL3::SDL3)
    if(WIN32)
        enable_language(RC)
        target_sources(BeebView PRIVATE packaging/windows/beebview.rc)
        target_include_directories(BeebView PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_options(BeebView PRIVATE -mwindows)
    endif()
    target_compile_options(BeebView PRIVATE -Wall -Wextra -Wpedantic)
    install(TARGETS BeebView RUNTIME DESTINATION bin)
    install(FILES packaging/linux/viewbbc.desktop
            DESTINATION share/applications)
else()
    message(STATUS "ViewBBC graphical frontend: disabled")
endif()

if(VIEWBBC_BUILD_NCURSES)
    find_package(Curses QUIET)
    if(NOT CURSES_FOUND)
        message(WARNING "ncurses not found; BeebView-terminal will not be built")
        set(VIEWBBC_BUILD_NCURSES OFF)
    endif()

    if(VIEWBBC_BUILD_NCURSES)
        add_executable(BeebView-terminal
            src/main_terminal.c
            src/frontend/ncurses/ncurses_frontend.c
            src/frontend/ncurses/terminal_launch.c
        )

        target_include_directories(BeebView-terminal PRIVATE
            ${CURSES_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        target_link_libraries(BeebView-terminal PRIVATE viewbbc_core ${CURSES_LIBRARIES})
        target_compile_options(BeebView-terminal PRIVATE -Wall -Wextra -Wpedantic)
        install(TARGETS BeebView-terminal RUNTIME DESTINATION bin)
        install(FILES packaging/linux/viewbbc-terminal.desktop
                DESTINATION share/applications)
    endif()
endif()

if(VIEWBBC_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(viewbbc_core_tests tests/test_core.c)
        target_include_directories(viewbbc_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(viewbbc_core_tests PRIVATE viewbbc_core)
        add_test(NAME viewbbc_core_tests COMMAND viewbbc_core_tests)
    endif()
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(CPACK_PACKAGE_NAME "beebview")
set(CPACK_PACKAGE_VENDOR "BeebView")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "BeebView maintainers")
set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
include(CPack)
