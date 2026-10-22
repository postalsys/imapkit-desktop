cmake_minimum_required(VERSION 3.24)
project(ImapKitDesktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

# the version of the app comes from the package.json at the root of the repository
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../package.json PACKAGE_JSON)
string(JSON APP_VERSION GET "${PACKAGE_JSON}" version)
string(REPLACE "." "," APP_VERSION_COMMAS "${APP_VERSION}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../package.json)

# JSON for the sidecar protocol
# the hashes pin what the release builds link, a changed download fails the build
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

# WebView2 SDK from NuGet, only the headers and the static loader are used
set(WEBVIEW2_VERSION 1.0.4258.31)
set(WEBVIEW2_DIR ${CMAKE_BINARY_DIR}/webview2-${WEBVIEW2_VERSION})
if(NOT EXISTS ${WEBVIEW2_DIR}/build/native/include/WebView2.h)
    file(DOWNLOAD https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION} ${CMAKE_BINARY_DIR}/webview2.nupkg
        TLS_VERIFY ON EXPECTED_HASH SHA256=56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9)
    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/webview2.nupkg DESTINATION ${WEBVIEW2_DIR})
endif()

add_executable(ImapKitDesktop WIN32
    src/main.cpp
    src/sidecar.cpp
    src/page.cpp
    src/server_page.cpp
    src/connections_page.cpp
    src/messages_page.cpp
    src/preview.cpp
    src/dialog.cpp
    res/app.rc
)

target_include_directories(ImapKitDesktop PRIVATE src ${WEBVIEW2_DIR}/build/native/include)
target_compile_definitions(ImapKitDesktop PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    APP_VERSION="${APP_VERSION}" APP_VERSION_COMMAS=${APP_VERSION_COMMAS})
target_compile_options(ImapKitDesktop PRIVATE /W4 /utf-8 /permissive-)
target_link_libraries(ImapKitDesktop PRIVATE
    nlohmann_json::nlohmann_json
    ${WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib
    comctl32 comdlg32 crypt32 shlwapi shell32 ole32 version
)
# the manifest comes from app.rc
target_link_options(ImapKitDesktop PRIVATE /MANIFEST:NO)
