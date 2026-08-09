cmake_minimum_required(VERSION 3.20)

project(ClonStarCitizen
    VERSION 0.1.0
    DESCRIPTION "Custom C++ ECS/DOD game engine (Vulkan)"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies -----------------------------------------------------------
# Ubuntu/Debian: sudo apt install libvulkan-dev libglfw3-dev
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

include(FetchContent)
FetchContent_Declare(
    flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
    GIT_TAG        v4.1.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(flecs)

# GLM (header-only) — pin stable tag; macros via target_compile_definitions + engine/math/glm.hpp
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# --- Shader compilation (GLSL → SPIR-V) -------------------------------------
find_program(GLSLC_EXECUTABLE
    NAMES glslc
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        /home/tenismiguel/android-sdk/ndk/28.2.13676358/shader-tools/linux-x86_64
        /usr/bin
        /usr/local/bin
)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found — install glslc/shaderc or set PATH to NDK shader-tools")
endif()
message(STATUS "  glslc:  ${GLSLC_EXECUTABLE}")

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/triangle.vert
    ${SHADER_SOURCE_DIR}/triangle.frag
)

set(SHADER_SPV_FILES "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_OUT ${SHADER_BINARY_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPV_OUT}
        COMMAND ${GLSLC_EXECUTABLE} -o ${SPV_OUT} ${SHADER}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER_NAME} → SPIR-V"
        VERBATIM
    )
    list(APPEND SHADER_SPV_FILES ${SPV_OUT})
endforeach()

add_custom_target(clon_shaders ALL DEPENDS ${SHADER_SPV_FILES})

# --- Engine sources ---------------------------------------------------------
set(ENGINE_SOURCES
    src/main.cpp
    src/engine/config/config.cpp
    src/engine/log/log.cpp
    src/engine/memory/arena.cpp
    src/engine/input/input.cpp
    src/engine/ecs/world.cpp
    src/engine/platform/window.cpp
    src/engine/vulkan/instance.cpp
    src/engine/vulkan/device.cpp
    src/engine/vulkan/renderer.cpp
    src/game/flight/flight.cpp
    src/game/character/character.cpp
    src/game/economy/economy.cpp
    src/game/world/world.cpp
    src/game/ui/ui.cpp
    src/game/audio/audio.cpp
    src/game/save/save.cpp
)

add_executable(clon_star_citizen ${ENGINE_SOURCES})
add_dependencies(clon_star_citizen clon_shaders)

target_include_directories(clon_star_citizen
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(clon_star_citizen
    PRIVATE
        Vulkan::Vulkan
        glfw
        flecs::flecs_static
        glm::glm
)

target_compile_definitions(clon_star_citizen
    PRIVATE
        $<$<CONFIG:Debug>:CSC_DEBUG=1>
        $<$<NOT:$<CONFIG:Debug>>:CSC_DEBUG=0>
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        CSC_SHADER_DIR="${SHADER_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(clon_star_citizen PRIVATE /W4 /permissive-)
else()
    target_compile_options(clon_star_citizen PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "ClonStarCitizen ${PROJECT_VERSION} — C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Vulkan: ${Vulkan_VERSION}")
message(STATUS "  Flecs:  v4.1.6 (FetchContent)")
message(STATUS "  GLM:    1.0.1 (FetchContent, FORCE_RADIANS + DEPTH_ZERO_TO_ONE)")
message(STATUS "  Shaders:${SHADER_BINARY_DIR}")
