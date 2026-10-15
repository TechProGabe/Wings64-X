# Wings64-X: default.xbe. Configure with nxdk's toolchain file (xbox/build.sh).
#
# Two kinds of code, two compile setups (docs/toolchain.md, PLAN.md D3):
#   game  the decomp (external/pw64-decomp, fetched and patched), xbox/src/game
#         and xbox/src/gfx: the game triple (i686-pc-windows-gnu
#         -mno-ms-bitfields), compiled by tools/xbox/compile_game.py and
#         linked here as objects
#   hw    xbox/src/hw: nxdk's own triple, talks to the kernel, pbkit and USB
cmake_minimum_required(VERSION 3.16)
project(wings64_x C)

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
# __FILE__ relative to the checkout: no builder's home directory in the XBE
get_filename_component(ROOT_ABS "${ROOT}" ABSOLUTE)
add_compile_options("-ffile-prefix-map=${ROOT_ABS}/=")
set(NXDK_DIR $ENV{NXDK_DIR})

# ---- pbkit, patched so a GPU fault can't freeze the console (Melee-X) ----
set(PBKIT_SRC ${NXDK_DIR}/lib/pbkit)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/pbkit/pbkit.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/pbkit
    COMMAND python3 ${ROOT}/tools/xbox/patch_pbkit.py ${PBKIT_SRC}/pbkit.c ${CMAKE_BINARY_DIR}/pbkit/pbkit.c
    DEPENDS ${PBKIT_SRC}/pbkit.c ${ROOT}/tools/xbox/patch_pbkit.py
    COMMENT "patch pbkit.c")
add_library(wx_pbkit STATIC
    ${CMAKE_BINARY_DIR}/pbkit/pbkit.c
    ${PBKIT_SRC}/pbkit_gamma.c ${PBKIT_SRC}/pbkit_dma.c ${PBKIT_SRC}/pbkit_draw.c
    ${PBKIT_SRC}/pbkit_print.c ${PBKIT_SRC}/pbkit_pushbuffer.c)
target_include_directories(wx_pbkit PRIVATE ${PBKIT_SRC})
target_compile_options(wx_pbkit PRIVATE -w)

# ---- game objects (tools/xbox/compile_game.py, incremental via depfiles) ----
# Absent until the decomp is fetched (tools/xbox/fetch_decomp.sh, M2): the
# XBE is then the platform layer alone, which boots to a card.
# XBOX_LTO (xbox/build.sh: XBOX_LTO=1): the game code as bitcode, ThinLTO at
# the link (tools/xbox/thinlto_link.py); its own object folder.
option(XBOX_LTO "ThinLTO over the game code" OFF)
set(GAME_OUT ${CMAKE_BINARY_DIR}/game)
set(GAME_ENV XBOX_LTO=)
if(XBOX_LTO)
    set(GAME_OUT ${GAME_OUT}-lto)
    set(GAME_ENV XBOX_LTO=1)
endif()
option(WX_GAME "link the game (needs external/pw64-decomp)" ON)
if(WX_GAME AND EXISTS ${ROOT}/external/pw64-decomp/src)
    set(WX_HAVE_GAME 1)
    add_custom_target(game_objs
        COMMAND ${CMAKE_COMMAND} -E env XBOX_GAME_OUT=${GAME_OUT} ${GAME_ENV} "XBOX_GAME_CFLAGS=${CMAKE_C_FLAGS}" python3 ${ROOT}/tools/xbox/compile_game.py
        WORKING_DIRECTORY ${ROOT}
        BYPRODUCTS ${GAME_OUT}/objects.txt ${GAME_OUT}/objects.stamp
        COMMENT "game code: compile"
        USES_TERMINAL)
endif()

# ---- hw: kernel, pbkit, AC97, USB (nxdk triple) ----
file(GLOB HW_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/hw/*.c)
add_library(wx_hw OBJECT ${HW_SOURCES})
target_include_directories(wx_hw PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hw
    ${NXDK_DIR}/lib/sdl/SDL2/include
    ${NXDK_DIR}/lib/usb/libusbohci/inc
    ${NXDK_DIR}/lib/usb/libusbohci_xbox
    ${NXDK_DIR}/lib/zlib/zlib)
target_compile_definitions(wx_hw PRIVATE XBOX "USBH_USE_EXTERNAL_CONFIG=\"usbh_config_xbox.h\"")
target_compile_options(wx_hw PRIVATE -O2 -march=pentium3 -ffunction-sections -Wall -Wno-unused-function
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/include/xbuiltin.h")
# the string routines must not be recognised as themselves (xhw_string.c)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/hw/xhw_string.c PROPERTIES
    COMPILE_OPTIONS "-fno-builtin;-ffreestanding" COMPILE_DEFINITIONS XBUILTIN_OFF)

# ---- link ----
add_executable(wings64_x $<TARGET_OBJECTS:wx_hw>)
target_link_libraries(wings64_x
    wx_pbkit
    ${NXDK_DIR}/lib/libSDL2.lib
    ${NXDK_DIR}/lib/libzlib.lib
    ${NXDK_DIR}/lib/libnxdk_automount_d.lib)
target_link_options(wings64_x PRIVATE
    -stack:1048576
    -include:_automount_d_drive
    -map:${CMAKE_BINARY_DIR}/wings64_x.map
    # every undefined symbol, not the first 20
    -errorlimit:0
    # never fold two identical functions into one address: the game
    # compares function pointers
    -opt:noicf)
if(WX_HAVE_GAME)
    add_dependencies(wings64_x game_objs)
    if(XBOX_LTO)
        set_property(TARGET wings64_x PROPERTY RULE_LAUNCH_LINK "python3 ${ROOT}/tools/xbox/thinlto_link.py")
    endif()
    set_property(TARGET wings64_x APPEND PROPERTY LINK_DEPENDS ${GAME_OUT}/objects.stamp)
    # The game objects come from outside CMake; the response file lists them.
    # Nothing calls the game yet (M2): wx_boot_game (xbox/src/game/wx_boot.c)
    # references bootproc, and -include keeps it, so the whole decomp is
    # resolved at link time (an undefined symbol fails the link).
    target_link_options(wings64_x PRIVATE "@${GAME_OUT}/objects.txt" -include:_wx_boot)
endif()
add_custom_command(TARGET wings64_x POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/xbe
    COMMAND ${NXDK_DIR}/tools/cxbe/cxbe "-OUT:${CMAKE_BINARY_DIR}/xbe/default.xbe" "-TITLE:Wings64-X" $<TARGET_FILE:wings64_x>
    COMMENT "cxbe -> xbe/default.xbe")
