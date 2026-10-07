cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")
  else()
    message(FATAL_ERROR "VITASDK environment variable is not set and CMAKE_TOOLCHAIN_FILE is not defined.")
  endif()
endif()

project(vita-luna C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("${VITASDK}/share/vita.cmake" OPTIONAL)

set(VITA_APP_NAME "Amazon Luna Client")
set(VITA_TITLEID "LUNA00001")
set(VITA_VERSION "01.00")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.c"
)

add_executable(vita-luna ${SOURCES})

target_link_libraries(vita-luna
    vita2d
    freetype
    png
    jpeg
    z
    SceAppMgr_stub
    SceAppUtil_stub
    SceCommonDialog_stub
    SceDisplay_stub
    SceGxm_stub
    SceCtrl_stub
    SceTouch_stub
    SceSysmodule_stub
    ScePgf_stub
    ScePvf_stub
    ScePower_stub
    SceNet_stub
    SceSsl_stub
    SceHttp_stub
    SceAudio_stub
    SceAvPlayer_stub
    SceVideodec_stub
)

if(COMMAND vita_create_self)
    vita_create_self(vita-luna.self vita-luna)
    vita_create_vpk(vita-luna.vpk ${VITA_TITLEID} vita-luna.self
        NAME ${VITA_APP_NAME}
        VERSION ${VITA_VERSION}
    )
endif()
