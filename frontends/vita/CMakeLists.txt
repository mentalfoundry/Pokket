if(NOT CMAKE_SYSTEM_NAME STREQUAL "Vita")
    message(STATUS "Skipping Vita frontend (configure with the vitasdk toolchain file to build it, see docs/vita_readme.md)")
    return()
endif()

include("${VITASDK}/share/vita.cmake" REQUIRED)

add_executable(pokket_vita main.c)
target_link_libraries(pokket_vita PRIVATE
    psemu
    vita2d
    SceDisplay_stub
    SceCtrl_stub
    SceGxm_stub
    SceSysmodule_stub
    SceCommonDialog_stub
)

vita_create_self(pokket_vita.self pokket_vita)
vita_create_vpk(pokket_vita.vpk "POKKETST01" pokket_vita.self
    VERSION "01.00"
    NAME "Pokket"
)
