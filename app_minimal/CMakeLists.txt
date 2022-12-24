cmake_minimum_required(VERSION 3.20)
project(app_minimal)
set(CMAKE_CXX_STANDARD 14)

add_compile_definitions(
  IMGUI_ENABLE_TEST_ENGINE
  IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
)

if(APPLE)
  add_compile_definitions(
    IMGUI_APP_GLFW_METAL
  )
endif()

include_directories(${CMAKE_SOURCE_DIR}/../../imgui)
include_directories(${CMAKE_SOURCE_DIR}/../../imgui/backends)
include_directories(${CMAKE_SOURCE_DIR}/..)
include_directories(${CMAKE_SOURCE_DIR}/../imgui_test_engine)
include_directories(${CMAKE_SOURCE_DIR}/../imgui_test_suite/thirdparty/implot)
include_directories(${CMAKE_SOURCE_DIR}/glfw/include)

add_executable(app_minimal
    ../../imgui/imconfig.h
    ../../imgui/imgui.h
    ../../imgui/imgui_internal.h
    ../../imgui/imstb_rectpack.h
    ../../imgui/imstb_textedit.h
    ../../imgui/imstb_truetype.h
    ../imgui_test_suite/thirdparty/implot/implot.h
    ../imgui_test_suite/thirdparty/implot/implot_internal.h
    ../shared/imgui_app.h app_minimal_imconfig.h
    ../../imgui/imgui.cpp
    ../../imgui/imgui_demo.cpp
    ../../imgui/imgui_draw.cpp
    ../../imgui/imgui_tables.cpp
    ../../imgui/imgui_widgets.cpp
    ../imgui_test_engine/imgui_capture_tool.cpp
    ../imgui_test_engine/imgui_te_context.cpp
    ../imgui_test_engine/imgui_te_coroutine.cpp
    ../imgui_test_engine/imgui_te_exporters.cpp
    ../imgui_test_engine/imgui_te_engine.cpp
    ../imgui_test_engine/imgui_te_exporters.cpp
    ../imgui_test_engine/imgui_te_perftool.cpp
    ../imgui_test_engine/imgui_te_ui.cpp
    ../imgui_test_engine/imgui_te_utils.cpp
    ../imgui_test_suite/thirdparty/implot/implot.cpp
    ../imgui_test_suite/thirdparty/implot/implot_demo.cpp
    ../imgui_test_suite/thirdparty/implot/implot_items.cpp
    ../shared/imgui_app.mm
    app_minimal_main.mm
    app_minimal_tests.cpp

  )

if (APPLE)
    file(GLOB GLFW_LIB "${CMAKE_CURRENT_SOURCE_DIR}/glfw/lib/*.dylib")
endif()

# set_target_properties(app_minimal PROPERTIES LINK_FLAGS "-framework MetalKit")
set_target_properties(app_minimal PROPERTIES LINK_FLAGS "-framework Foundation -framework QuartzCore -framework Metal -framework CoreVideo -framework IOKit -framework Cocoa -framework MetalKit")

target_link_libraries (app_minimal LINK_PUBLIC ${GLFW_LIB})
