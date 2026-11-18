cmake_minimum_required(VERSION 3.24)

project(sway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_executable(sway
    src/main.cpp
    src/vma_impl.cpp
)

# Shaders are compiled by the build, not by hand. Compiling them manually
# works until the first time you forget, and then you debug a stale .spv.
find_program(SLANGC slangc REQUIRED HINTS "$ENV{VULKAN_SDK}/Bin")

set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

add_custom_command(
    OUTPUT "${SHADER_OUT_DIR}/gradient.spv"
    # -fvk-use-entrypoint-name keeps the source name: without it a lone entry
    # point is renamed to "main" in the SPIR-V and the pipeline finds nothing.
    COMMAND "${SLANGC}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gradient.slang"
            -target spirv -fvk-use-entrypoint-name
            -o "${SHADER_OUT_DIR}/gradient.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gradient.slang"
    COMMENT "slangc gradient.slang"
    VERBATIM
)

add_custom_target(shaders DEPENDS "${SHADER_OUT_DIR}/gradient.spv")
add_dependencies(sway shaders)

target_link_libraries(sway PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    GPUOpen::VulkanMemoryAllocator
)

# Strips the constructors from vk:: structs so they are aggregates, which is
# what C++20 designated initializers require. Must apply to every translation
# unit: defining it in some but not others is an ODR violation.
target_compile_definitions(sway PRIVATE
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)
