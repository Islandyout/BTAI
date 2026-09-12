cmake_minimum_required(VERSION 3.25)
project(BTAI VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BTAI_ENABLE_VALIDATION "Enable Vulkan validation" ON)
option(BTAI_BUILD_TESTS "Build BTAI tests" ON)

find_package(Vulkan 1.2 REQUIRED)
find_package(Threads REQUIRED)
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
  include(FetchContent)
  FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)
endif()
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(json)
endif()
include(FetchContent)
FetchContent_Declare(tinygltf GIT_REPOSITORY https://github.com/syoyo/tinygltf.git GIT_TAG v2.9.5 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tinygltf)

find_program(BTAI_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT BTAI_GLSLC)
  message(FATAL_ERROR "glslc was not found. Install the Vulkan SDK or the glslc package.")
endif()

set(BTAI_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(BTAI_SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.vert" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.frag")
set(BTAI_SHADER_BINARIES "${BTAI_SHADER_OUTPUT_DIR}/basic.vert.spv" "${BTAI_SHADER_OUTPUT_DIR}/basic.frag.spv")
add_custom_command(OUTPUT ${BTAI_SHADER_BINARIES}
  COMMAND ${CMAKE_COMMAND} -E make_directory "${BTAI_SHADER_OUTPUT_DIR}"
  COMMAND ${BTAI_GLSLC} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.vert" -o "${BTAI_SHADER_OUTPUT_DIR}/basic.vert.spv"
  COMMAND ${BTAI_GLSLC} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.frag" -o "${BTAI_SHADER_OUTPUT_DIR}/basic.frag.spv"
  DEPENDS ${BTAI_SHADER_SOURCES} VERBATIM)
add_custom_target(btai_shaders ALL DEPENDS ${BTAI_SHADER_BINARIES})

add_executable(btai
  src/main.cpp
  src/core/Engine.cpp
  src/core/Log.cpp
  src/jobs/JobSystem.cpp
  src/physics/Physics.cpp
  src/world/Streaming.cpp
  src/input/Input.cpp
  src/render/Camera.cpp
  src/ai/CommandInterpreter.cpp
  src/render/VulkanRendererStateOfArt.cpp
  src/render/VulkanRendererGltf.cpp
  src/platform/Window.cpp
  src/assets/AssetManager.cpp)
add_dependencies(btai btai_shaders)
target_include_directories(btai PRIVATE include ${tinygltf_SOURCE_DIR})
target_link_libraries(btai PRIVATE Vulkan::Vulkan glfw nlohmann_json::nlohmann_json Threads::Threads)
target_compile_definitions(btai PRIVATE GLFW_INCLUDE_VULKAN BTAI_SHADER_DIR="${BTAI_SHADER_OUTPUT_DIR}")
if(BTAI_ENABLE_VALIDATION)
  target_compile_definitions(btai PRIVATE BTAI_ENABLE_VALIDATION=1)
endif()
if(MSVC)
  target_compile_options(btai PRIVATE /W4 /permissive-)
else()
  target_compile_options(btai PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(BTAI_BUILD_TESTS)
  enable_testing()
  add_executable(btai_tests tests/EngineCoreTests.cpp src/jobs/JobSystem.cpp src/physics/Physics.cpp src/world/Streaming.cpp src/ai/CommandInterpreter.cpp src/assets/AssetManager.cpp)
  target_include_directories(btai_tests PRIVATE include ${tinygltf_SOURCE_DIR})
  target_link_libraries(btai_tests PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
  if(MSVC)
    target_compile_options(btai_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(btai_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
  add_test(NAME btai_core COMMAND btai_tests)
endif()

include(GNUInstallDirs)
install(TARGETS btai RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${BTAI_SHADER_BINARIES} DESTINATION ${CMAKE_INSTALL_DATADIR}/btai/shaders)
