cmake_minimum_required(VERSION 3.14)
project(xahball)

string(TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWER)

# A 7 MHz 68000 cannot sustain the match loop with an unoptimized build.
# Default single-config generators to the toolchain's tuned Release flags.
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT AMIGA)
	message(SEND_ERROR "This project only compiles for Amiga")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAMIGA -Wall -Wextra -fomit-frame-pointer")

file(GLOB_RECURSE SOURCES src/*.c)
file(GLOB_RECURSE HEADERS src/*.h)

include_directories(${PROJECT_SOURCE_DIR}/src)

set(RES_DIR ${CMAKE_CURRENT_LIST_DIR}/res)
set(DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
# audio_conv crashes on missing output dir - create it up front
file(MAKE_DIRECTORY ${DATA_DIR}/sfx)

# ACE
add_subdirectory(deps/ace)
include_directories(deps/ace/include)

set(TARGET_NAME ${PROJECT_NAME_LOWER})

# elf2hunk step for Bartman compiler
set(GAME_LINKED ${TARGET_NAME}.elf)
set(GAME_EXE ${TARGET_NAME}.exe)
add_executable(${GAME_LINKED} ${SOURCES} ${HEADERS})
add_custom_command(TARGET ${GAME_LINKED} POST_BUILD
	COMMAND ${ELF2HUNK} ${GAME_LINKED} ${GAME_EXE}
)
add_custom_command(TARGET ${GAME_LINKED} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/dist
	COMMAND ${CMAKE_COMMAND} -E copy ${GAME_EXE} ${CMAKE_CURRENT_BINARY_DIR}/dist
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${DATA_DIR} ${CMAKE_CURRENT_BINARY_DIR}/dist/data
)

target_link_libraries(${GAME_LINKED} ace)

# Assets
convertPalette(${GAME_LINKED} ${RES_DIR}/game.gpl ${DATA_DIR}/game.plt)

# Sprites: interleaved .bm + .msk masks (mask color stripped by -mc)
# Player discs are numbered per slot (teamA1..3 / teamB1..3)
convertBitmaps(
	TARGET ${GAME_LINKED}
	PALETTE ${RES_DIR}/game.gpl
	MASK_COLOR "#ff00ff"
	INTERLEAVED
	SOURCES
		${RES_DIR}/sprites/teamA1.png
		${RES_DIR}/sprites/teamA2.png
		${RES_DIR}/sprites/teamA3.png
		${RES_DIR}/sprites/teamB1.png
		${RES_DIR}/sprites/teamB2.png
		${RES_DIR}/sprites/teamB3.png
		${RES_DIR}/sprites/teamA1-flash.png
		${RES_DIR}/sprites/teamA2-flash.png
		${RES_DIR}/sprites/teamA3-flash.png
		${RES_DIR}/sprites/teamB1-flash.png
		${RES_DIR}/sprites/teamB2-flash.png
		${RES_DIR}/sprites/teamB3-flash.png
		${RES_DIR}/sprites/teamA1-ready.png
		${RES_DIR}/sprites/teamA2-ready.png
		${RES_DIR}/sprites/teamA3-ready.png
		${RES_DIR}/sprites/teamB1-ready.png
		${RES_DIR}/sprites/teamB2-ready.png
		${RES_DIR}/sprites/teamB3-ready.png
	DESTINATIONS
		${DATA_DIR}/teamA1.bm
		${DATA_DIR}/teamA2.bm
		${DATA_DIR}/teamA3.bm
		${DATA_DIR}/teamB1.bm
		${DATA_DIR}/teamB2.bm
		${DATA_DIR}/teamB3.bm
		${DATA_DIR}/teamA1-flash.bm
		${DATA_DIR}/teamA2-flash.bm
		${DATA_DIR}/teamA3-flash.bm
		${DATA_DIR}/teamB1-flash.bm
		${DATA_DIR}/teamB2-flash.bm
		${DATA_DIR}/teamB3-flash.bm
		${DATA_DIR}/teamA1-ready.bm
		${DATA_DIR}/teamA2-ready.bm
		${DATA_DIR}/teamA3-ready.bm
		${DATA_DIR}/teamB1-ready.bm
		${DATA_DIR}/teamB2-ready.bm
		${DATA_DIR}/teamB3-ready.bm
	MASKS
		${DATA_DIR}/teamA1.msk
		${DATA_DIR}/teamA2.msk
		${DATA_DIR}/teamA3.msk
		${DATA_DIR}/teamB1.msk
		${DATA_DIR}/teamB2.msk
		${DATA_DIR}/teamB3.msk
		${DATA_DIR}/teamA1-flash.msk
		${DATA_DIR}/teamA2-flash.msk
		${DATA_DIR}/teamA3-flash.msk
		${DATA_DIR}/teamB1-flash.msk
		${DATA_DIR}/teamB2-flash.msk
		${DATA_DIR}/teamB3-flash.msk
		${DATA_DIR}/teamA1-ready.msk
		${DATA_DIR}/teamA2-ready.msk
		${DATA_DIR}/teamA3-ready.msk
		${DATA_DIR}/teamB1-ready.msk
		${DATA_DIR}/teamB2-ready.msk
		${DATA_DIR}/teamB3-ready.msk
)

convertFont(
	TARGET ${GAME_LINKED}
	SOURCE ${RES_DIR}/silkscreen DESTINATION ${DATA_DIR}/silkscreen.fnt
)

convertAudio(TARGET ${GAME_LINKED} SOURCE ${RES_DIR}/sfx/move.wav DESTINATION ${DATA_DIR}/sfx/move.sfx PTPLAYER NORMALIZE)
convertAudio(TARGET ${GAME_LINKED} SOURCE ${RES_DIR}/sfx/confirm.wav DESTINATION ${DATA_DIR}/sfx/confirm.sfx PTPLAYER NORMALIZE)
convertAudio(TARGET ${GAME_LINKED} SOURCE ${RES_DIR}/sfx/back.wav DESTINATION ${DATA_DIR}/sfx/back.sfx PTPLAYER NORMALIZE)
convertAudio(TARGET ${GAME_LINKED} SOURCE ${RES_DIR}/sfx/kick.wav DESTINATION ${DATA_DIR}/sfx/kick.sfx PTPLAYER NORMALIZE)
convertAudio(TARGET ${GAME_LINKED} SOURCE ${RES_DIR}/sfx/bounce.wav DESTINATION ${DATA_DIR}/sfx/bounce.sfx PTPLAYER NORMALIZE)
convertAudio(TARGET ${GAME_LINKED} SOURCE ${RES_DIR}/sfx/goal.wav DESTINATION ${DATA_DIR}/sfx/goal.sfx PTPLAYER NORMALIZE)
convertAudio(TARGET ${GAME_LINKED} SOURCE ${RES_DIR}/sfx/whistle.wav DESTINATION ${DATA_DIR}/sfx/whistle.sfx PTPLAYER NORMALIZE)
convertAudio(TARGET ${GAME_LINKED} SOURCE ${RES_DIR}/sfx/ace.wav DESTINATION ${DATA_DIR}/sfx/ace.sfx PTPLAYER NORMALIZE)

# ACE intro logo splash (GermZ-style: display palette stays black, colors
# come from copper blocks over the logo - A=1, C=2, E/POWERED=3). The .gpl
# is padded to 32 entries so bitmap_conv emits a 5bpp bitmap: depth must
# match the framebuffer or blitCopy() falls back to the slow per-plane
# mixed-interleaved path instead of the single-blit fast path.
convertBitmaps(
	TARGET ${GAME_LINKED}
	PALETTE ${RES_DIR}/logo/ace.gpl
	INTERLEAVED
	SOURCES ${RES_DIR}/logo/ace.png
	DESTINATIONS ${DATA_DIR}/ace.bm
)
# Title art: full-screen 320x256x5bpp, interleaved like the framebuffer so
# blitCopyAligned() takes the fast aligned path. The palette is picked from
# the image itself and also shipped as .plt - the intro loads it at runtime
# (paletteLoadFromPath) so bitmap and palette can never drift apart.
# CONVERT_COLORS: the .gpl keeps exact 8-bit RGBs (bitmap_conv matches them
# exactly), the .plt truncates to 4-bit OCS channels.
convertPalette(${GAME_LINKED} ${RES_DIR}/logo/title.gpl ${DATA_DIR}/title.plt CONVERT_COLORS)
convertBitmaps(
	TARGET ${GAME_LINKED}
	PALETTE ${RES_DIR}/logo/title.gpl
	INTERLEAVED
	SOURCES ${RES_DIR}/logo/title.png
	DESTINATIONS ${DATA_DIR}/title.bm
)

# Menu music: mod_tool splits the ProTracker mod into a fast-mem .mod
# structure + chip samplepack for ptplayer (chaosArena-style pipeline)
mergeMods(
	TARGET ${GAME_LINKED}
	SOURCES ${RES_DIR}/music/menu.mod
	DESTINATIONS ${DATA_DIR}/menu.mod
	SAMPLE_PACK ${DATA_DIR}/menu.samplepack
)
