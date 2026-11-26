cmake_minimum_required(VERSION 3.20)
project(SolarSandbox LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---- Third-party (downloaded at configure time) -------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)  # X11 only on Linux (no extra deps)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
FetchContent_Declare(glm  GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw glm imgui)

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC glfw)

# glad (pre-generated, OpenGL 3.3 core) is vendored in third_party/glad
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# ---- Simulation core (no graphics) --------------------------------------
add_library(solar_core STATIC
  src/catalog.cpp
  src/orbit.cpp
  src/sim.cpp)
target_include_directories(solar_core PUBLIC src)
target_link_libraries(solar_core PUBLIC glm::glm)
if(MSVC)
  target_compile_options(solar_core PRIVATE /W3 /fp:fast)
else()
  target_compile_options(solar_core PRIVATE -Wall -Wextra $<$<NOT:$<CONFIG:Debug>>:-O2>)
endif()

# ---- The app -----------------------------------------------------------
add_executable(solar_sandbox
  src/main.cpp
  src/renderer.cpp
  src/ui.cpp
  src/png.cpp)
target_link_libraries(solar_sandbox PRIVATE solar_core glad glfw imgui_lib)
target_compile_definitions(solar_sandbox PRIVATE
  SOLAR_SOURCE_DIR="${CMAKE_SOURCE_DIR}" GLM_ENABLE_EXPERIMENTAL)
if(MSVC)
  target_compile_options(solar_sandbox PRIVATE /W3 /utf-8)
  set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT solar_sandbox)
else()
  target_compile_options(solar_sandbox PRIVATE -Wall -Wextra)
endif()

# Shaders are loaded at runtime; copy them next to the executable.
add_custom_command(TARGET solar_sandbox POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:solar_sandbox>/shaders)

# ---- Headless physics checks -------------------------------------------
add_executable(sim_test tests/sim_test.cpp)
target_link_libraries(sim_test PRIVATE solar_core)
enable_testing()
add_test(NAME sim_test COMMAND sim_test)
