cmake_minimum_required(VERSION 3.24)

project(sway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_executable(sway src/main.cpp)

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
