cmake_minimum_required(VERSION 3.20)
project(VTracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "VTracer is Windows-only (uses Win32 + WebView2).")
endif()

option(NIDEK_BUILD_FRONTEND "Run npm to build the Vue frontend at configure time" ON)

# --- Locate WebView2 SDK ---------------------------------------------------
set(WEBVIEW2_SDK "" CACHE PATH "Path to the unpacked Microsoft.Web.WebView2 NuGet package")

if(NOT WEBVIEW2_SDK)
    foreach(_candidate
        "$ENV{WEBVIEW2_SDK}"
        "${CMAKE_SOURCE_DIR}/third_party/webview2"
        "${CMAKE_SOURCE_DIR}/external/webview2")
        if(_candidate AND EXISTS "${_candidate}/build/native/include/WebView2.h")
            set(WEBVIEW2_SDK "${_candidate}" CACHE PATH "" FORCE)
            break()
        endif()
    endforeach()
endif()

if(NOT WEBVIEW2_SDK OR NOT EXISTS "${WEBVIEW2_SDK}/build/native/include/WebView2.h")
    message(FATAL_ERROR
        "WebView2 SDK not found.\n"
        "Pass -DWEBVIEW2_SDK=C:/path/to/unpacked/webview2 (the folder that contains build/native/include/WebView2.h)\n"
        "Get it from https://www.nuget.org/packages/Microsoft.Web.WebView2 — rename .nupkg to .zip and extract.")
endif()

set(WEBVIEW2_INCLUDE "${WEBVIEW2_SDK}/build/native/include")
set(WEBVIEW2_LIB_DIR "${WEBVIEW2_SDK}/build/native/x64")

# Prefer the static loader for a true single-exe build; fall back to the dll-import lib.
set(_static_lib "${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib")
set(_import_lib "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll.lib")
if(EXISTS "${_static_lib}")
    set(WEBVIEW2_LOADER "${_static_lib}")
    set(WEBVIEW2_LOADER_KIND "static")
elseif(EXISTS "${_import_lib}")
    set(WEBVIEW2_LOADER "${_import_lib}")
    set(WEBVIEW2_LOADER_KIND "dll")
    message(WARNING "Static WebView2 loader not found in SDK — using DLL variant. WebView2Loader.dll must be next to the exe.")
else()
    message(FATAL_ERROR "Neither WebView2LoaderStatic.lib nor WebView2Loader.dll.lib found in ${WEBVIEW2_LIB_DIR}")
endif()

# --- Frontend build --------------------------------------------------------
set(FRONTEND_DIR "${CMAKE_SOURCE_DIR}/frontend")
set(FRONTEND_DIST "${FRONTEND_DIR}/dist")
set(FRONTEND_HTML "${FRONTEND_DIST}/index.html")

if(NIDEK_BUILD_FRONTEND)
    find_program(NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)
    file(GLOB_RECURSE FRONTEND_SOURCES CONFIGURE_DEPENDS
         "${FRONTEND_DIR}/src/*.ts"
         "${FRONTEND_DIR}/src/*.vue"
         "${FRONTEND_DIR}/src/*.css")
    add_custom_command(
        OUTPUT "${FRONTEND_HTML}"
        COMMAND "${NPM_EXECUTABLE}" install
        COMMAND "${NPM_EXECUTABLE}" run build
        WORKING_DIRECTORY "${FRONTEND_DIR}"
        DEPENDS
            "${FRONTEND_DIR}/package.json"
            "${FRONTEND_DIR}/vite.config.ts"
            "${FRONTEND_DIR}/tsconfig.json"
            "${FRONTEND_DIR}/index.html"
            ${FRONTEND_SOURCES}
        COMMENT "Building Vue frontend (npm install + npm run build)"
        VERBATIM)
    add_custom_target(frontend DEPENDS "${FRONTEND_HTML}")
else()
    add_custom_target(frontend)
endif()

set(APP_ICON "${CMAKE_SOURCE_DIR}/backend/resources/app.ico")
set(GENERATED_RC "${CMAKE_BINARY_DIR}/app.rc")
configure_file("${CMAKE_SOURCE_DIR}/backend/resources/app.rc.in" "${GENERATED_RC}" @ONLY)

# The .rc embeds index.html and app.ico, so the .res must rebuild when either
# changes. CMake doesn't infer this automatically.
set_source_files_properties("${GENERATED_RC}" PROPERTIES
    OBJECT_DEPENDS "${FRONTEND_HTML};${APP_ICON}")

# --- Executable ------------------------------------------------------------
add_executable(VTracer WIN32
    backend/src/main.cpp
    backend/src/AppWindow.cpp
    backend/src/SerialPort.cpp
    backend/src/NidekTracer.cpp
    backend/src/ModelDatabase.cpp
    backend/src/Util.cpp
    "${GENERATED_RC}")

add_dependencies(VTracer frontend)

target_include_directories(VTracer PRIVATE
    backend/src
    "${WEBVIEW2_INCLUDE}")

target_compile_definitions(VTracer PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)

if(MSVC)
    target_compile_options(VTracer PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

target_link_libraries(VTracer PRIVATE
    "${WEBVIEW2_LOADER}"
    user32 shell32 ole32 oleaut32 advapi32 version comctl32)

set_target_properties(VTracer PROPERTIES
    OUTPUT_NAME "VTracer"
    VS_DPI_AWARE "PerMonitor")

# Copy every catalog CSV into <exe-dir>/catalogs/ on every build.
file(GLOB CATALOG_CSVS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/data/*.csv")
add_custom_command(TARGET VTracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:VTracer>/catalogs"
    COMMENT "Preparing catalogs/")
foreach(_csv IN LISTS CATALOG_CSVS)
    get_filename_component(_csv_name "${_csv}" NAME)
    add_custom_command(TARGET VTracer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_csv}" "$<TARGET_FILE_DIR:VTracer>/catalogs/${_csv_name}"
        COMMENT "Catalog: ${_csv_name}")
endforeach()

# If we're using the dll-import loader, copy the DLL too.
if(WEBVIEW2_LOADER_KIND STREQUAL "dll")
    add_custom_command(TARGET VTracer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll"
            "$<TARGET_FILE_DIR:VTracer>/WebView2Loader.dll"
        COMMENT "Copying WebView2Loader.dll")
endif()

message(STATUS "WebView2 SDK     : ${WEBVIEW2_SDK}")
message(STATUS "WebView2 loader  : ${WEBVIEW2_LOADER} (${WEBVIEW2_LOADER_KIND})")
message(STATUS "Frontend dist    : ${FRONTEND_DIST}")
