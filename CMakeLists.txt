cmake_minimum_required(VERSION 3.25)
project(BTAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BTAI_ENABLE_VALIDATION "Enable Vulkan validation" ON)
option(BTAI_BUILD_TESTS "Build BTAI tests" ON)

find_package(Vulkan 1.2 REQUIRED)
find_package(glfw3 3.4 QUIET)
if(NOT glfw3_FOUND)
  include(FetchContent)
  FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)
endif()

add_executable(btai
  src/main.cpp
  src/core/Engine.cpp
  src/core/Log.cpp
  src/jobs/JobSystem.cpp
  src/render/VulkanRenderer.cpp
  src/platform/Window.cpp
)

target_include_directories(btai PRIVATE include)
target_link_libraries(btai PRIVATE Vulkan::Vulkan glfw)
target_compile_definitions(btai PRIVATE GLFW_INCLUDE_VULKAN)
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
  add_executable(btai_tests tests/EngineCoreTests.cpp)
  target_include_directories(btai_tests PRIVATE include)
  if(MSVC)
    target_compile_options(btai_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(btai_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
  add_test(NAME btai_core COMMAND btai_tests)
endif()

include(GNUInstallDirs)
install(TARGETS btai RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
