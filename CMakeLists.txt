cmake_minimum_required(VERSION 3.27)

project(HelloVulkan)

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(HelloVulkan ${SOURCES})

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)

set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)

add_subdirectory(external/SDL)
find_package(Vulkan REQUIRED volk shaderc_combined)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_property(TARGET HelloVulkan PROPERTY DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

find_program(GLSL_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

## find all the shader files under the shaders folder
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_SOURCE_DIR}/shaders/*.comp"
    "${CMAKE_SOURCE_DIR}/shaders/*.geom"
    "${CMAKE_SOURCE_DIR}/shaders/*.tesc"
    "${CMAKE_SOURCE_DIR}/shaders/*.tese"
    )

target_include_directories(HelloVulkan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)

target_link_libraries(HelloVulkan PRIVATE
    Vulkan::Vulkan
    Vulkan::volk
    Vulkan::shaderc_combined
    SDL3::SDL3)

## iterate each shader
foreach(GLSL ${GLSL_SOURCE_FILES})
  message(STATUS "BUILDING SHADER")
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/${FILE_NAME}.spv")
  message(STATUS ${GLSL})
  ##execute glslang command to compile that specific shader
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
    )