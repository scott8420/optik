cmake_minimum_required(VERSION 3.20)
project(optik VERSION 0.2.0 LANGUAGES CXX)

# Optik -- an OCR workbench for scanned books, on FineReader's model:
# the page is always in view, the engine proposes areas and text, the author
# adjusts and proofs, the book exports to ODT for Writer.
#
# Layers (ARCHITECTURE.md), each a target, each linking only downward:
#
#   optik_scan   libs/scan  spread / split / intake -- NO dependencies
#   optik_image  libs/scan  Image.cpp, the Leptonica decode seam
#   optik_ocr    libs/ocr   the Tesseract seam (Layout.hpp is engine-free)
#   optik_core   core/      Book, Journal, Jobs, Export -- GTK-FREE by design:
#                           no gtkmm on its link line, so a UI include fails here
#   optik        app/       the window (gtkmm 4)
#   optik_selftest          headless: scan + ocr + core

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
add_compile_options(-Wall -Wextra)

# The sweep's build (s26): AddressSanitizer + UBSan on every target, so an
# out-of-bounds read is a red line and not "harmless garbage" (CANON s23).
#   cmake -B build-asan -DOPTIK_SANITIZE=ON     (scripts/sweep.sh does this)
option(OPTIK_SANITIZE "Build with ASan + UBSan" OFF)
if(OPTIK_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# The build stamp the About line and the selftest print. Bumped per session.
set(OPTIK_BUILD "s29")
set(OPTIK_APP_ID "io.github.example.Optik")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LEPT REQUIRED IMPORTED_TARGET lept)
pkg_check_modules(TESS REQUIRED IMPORTED_TARGET tesseract)
pkg_check_modules(ZIP  REQUIRED IMPORTED_TARGET libzip)
pkg_check_modules(HUNSPELL REQUIRED IMPORTED_TARGET hunspell)   # s27: the spelling pass

# -- libs/scan ------------------------------------------------------------------
add_library(optik_scan STATIC
    libs/scan/src/Spread.cpp
    libs/scan/src/Split.cpp
    libs/scan/src/Intake.cpp
)
target_include_directories(optik_scan PUBLIC libs/scan/include)

add_library(optik_image STATIC libs/scan/src/Image.cpp)
target_link_libraries(optik_image PUBLIC optik_scan PkgConfig::LEPT)

# -- libs/ocr -------------------------------------------------------------------
add_library(optik_ocr STATIC libs/ocr/Ocr.cpp libs/ocr/Slant.cpp libs/ocr/Condition.cpp)
target_include_directories(optik_ocr PUBLIC libs)
target_link_libraries(optik_ocr PUBLIC PkgConfig::TESS PkgConfig::LEPT)

# -- core -----------------------------------------------------------------------
add_library(optik_core STATIC
    core/Book.cpp
    core/Undo.cpp
    core/Jobs.cpp
    core/Reading.cpp
    core/Passes.cpp
    core/Recent.cpp
    core/Audit.cpp
    core/PageOps.cpp
    core/Picture.cpp
    core/Export.cpp
    core/Spell.cpp
    core/Find.cpp
    core/Format.cpp
)
target_include_directories(optik_core PUBLIC . libs PRIVATE third_party)
target_link_libraries(optik_core PUBLIC optik_image optik_ocr Threads::Threads
                                 PRIVATE PkgConfig::ZIP PkgConfig::HUNSPELL)
target_compile_definitions(optik_core PUBLIC OPTIK_BUILD="${OPTIK_BUILD}")

# -- selftest -------------------------------------------------------------------
add_executable(optik_selftest
    tests/selftest.cpp
    tests/selftest_scan.cpp
    tests/selftest_core.cpp
    tests/selftest_passes.cpp
    tests/selftest_recent.cpp
    tests/selftest_audit.cpp
    tests/selftest_pageops.cpp
    tests/selftest_italic.cpp
    tests/selftest_find.cpp
    tests/selftest_format.cpp
    tests/selftest_condition.cpp
    tests/selftest_export.cpp
)
target_include_directories(optik_selftest PRIVATE tests)
target_link_libraries(optik_selftest PRIVATE optik_core PkgConfig::ZIP)   # s27: reads an ODT back
target_compile_definitions(optik_selftest PRIVATE
    OPTIK_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

# -- the app --------------------------------------------------------------------
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)
find_package(spdlog REQUIRED)   # the CMake package: it carries fmt (s21 note)

find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(GRES_XML ${CMAKE_CURRENT_SOURCE_DIR}/resources/optik.gresource.xml)
set(GRES_C   ${CMAKE_CURRENT_BINARY_DIR}/optik_resources.c)
add_custom_command(
    OUTPUT ${GRES_C}
    COMMAND ${GLIB_COMPILE_RESOURCES} --target=${GRES_C} --generate-source
            --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/resources ${GRES_XML}
    DEPENDS ${GRES_XML} ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps/optik-logo-symbolic.svg)
set_source_files_properties(${GRES_C} PROPERTIES LANGUAGE CXX)

file(GLOB APP_SOURCES CONFIGURE_DEPENDS app/*.cpp)
add_executable(optik ${APP_SOURCES} ${GRES_C})
target_include_directories(optik PRIVATE app)
target_link_libraries(optik PRIVATE optik_core PkgConfig::GTKMM spdlog::spdlog)
target_compile_definitions(optik PRIVATE OPTIK_APP_ID="${OPTIK_APP_ID}")
