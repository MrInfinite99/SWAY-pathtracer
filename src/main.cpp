#include <vulkan/vulkan_raii.hpp>

#include <cstdio>

int main()
{
    uint32_t apiVersion = 0;
    VkResult result = vkEnumerateInstanceVersion(&apiVersion);
    if (result != VK_SUCCESS) {
        std::printf("vkEnumerateInstanceVersion failed: %d\n", result);
        return 1;
    }

    std::printf("Vulkan instance version: %u.%u.%u\n",
                VK_API_VERSION_MAJOR(apiVersion),
                VK_API_VERSION_MINOR(apiVersion),
                VK_API_VERSION_PATCH(apiVersion));

    return 0;
}
