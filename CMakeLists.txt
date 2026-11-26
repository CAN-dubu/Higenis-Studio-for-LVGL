cmake_minimum_required(VERSION 3.20)
project(vellum C CXX RC)

# 정적 런타임: 재배포 패키지 없이 exe 하나로 실행
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(TP ${CMAKE_SOURCE_DIR}/third_party)

# ---- LVGL (실제 렌더러) ----
file(GLOB_RECURSE LVGL_SRC CONFIGURE_DEPENDS ${TP}/lvgl/src/*.c)
add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl PUBLIC
  ${CMAKE_SOURCE_DIR}/src/lvconf ${TP}/lvgl ${TP}/lvgl/include ${TP}/lvgl/include/lvgl)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
if(MSVC)
  target_compile_options(lvgl PRIVATE /W0 /utf-8)
endif()

# ---- Dear ImGui (에디터 외형) ----
add_library(imgui STATIC
  ${TP}/imgui/imgui.cpp ${TP}/imgui/imgui_draw.cpp ${TP}/imgui/imgui_tables.cpp
  ${TP}/imgui/imgui_widgets.cpp ${TP}/imgui/misc/cpp/imgui_stdlib.cpp
  ${TP}/imgui/backends/imgui_impl_win32.cpp ${TP}/imgui/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${TP}/imgui ${TP}/imgui/backends ${TP}/imgui/misc/cpp)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# 내보내는 글자 꾸밈 코드: 미리보기가 쓰는 바로 그 파일을 문자열로 담는다
file(READ ${CMAKE_SOURCE_DIR}/src/higenis_fx.c VELLUM_FX_C)
file(READ ${CMAKE_SOURCE_DIR}/src/higenis_fx.h VELLUM_FX_H)
file(WRITE ${CMAKE_BINARY_DIR}/higenis_fx_text.h.new "// CMake가 만든 파일\nstatic const char kHigenisFxC[] = R\"VFX(${VELLUM_FX_C})VFX\";\nstatic const char kHigenisFxH[] = R\"VFX(${VELLUM_FX_H})VFX\";\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/higenis_fx_text.h.new ${CMAKE_BINARY_DIR}/higenis_fx_text.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/higenis_fx.c ${CMAKE_SOURCE_DIR}/src/higenis_fx.h)

# ---- Higenis Studio for LVGL ----
add_executable(vellum WIN32
  src/main.cpp src/model.cpp src/codegen.cpp src/arduino.cpp src/image.cpp src/fontface.cpp src/higenis_fx.c src/fontgen.cpp src/fonthint.cpp src/ui_font.cpp src/lvhost.cpp src/smooth.cpp
  src/ui_kit.cpp src/ui_canvas.cpp src/ui_panels.cpp src/ui_sim.cpp src/vellum.rc)
# 헤더가 바뀌면 반드시 다시 컴파일한다. Ninja의 자동 감지는 cl.exe의 안내문(/showIncludes)을 읽는데,
# 한국어 Visual Studio에서는 콘솔 코드 페이지에 따라 글자가 달라져 조용히 놓칠 수 있다.
file(GLOB VELLUM_HDR CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.h)
list(APPEND VELLUM_HDR ${CMAKE_BINARY_DIR}/higenis_fx_text.h)
set(LV_CONF_HDR ${CMAKE_SOURCE_DIR}/src/lvconf/lv_conf.h)
get_target_property(VELLUM_SRC vellum SOURCES)
set_source_files_properties(${VELLUM_SRC} PROPERTIES OBJECT_DEPENDS "${VELLUM_HDR};${LV_CONF_HDR}")
set_source_files_properties(${LVGL_SRC} PROPERTIES OBJECT_DEPENDS ${LV_CONF_HDR})
set_target_properties(vellum PROPERTIES OUTPUT_NAME "HigenisStudio")
target_include_directories(vellum PRIVATE ${TP}/stb ${CMAKE_BINARY_DIR})
target_link_libraries(vellum PRIVATE imgui lvgl d3d11 dxgi d3dcompiler comdlg32 ole32 shell32 dwmapi imm32 windowscodecs)
if(MSVC)
  target_compile_options(vellum PRIVATE /utf-8 /W3 /permissive-)
  target_compile_definitions(vellum PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()
add_custom_command(TARGET vellum POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:vellum>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:vellum>/assets)
