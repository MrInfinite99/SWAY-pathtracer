#ifndef ALLOC_H
#define ALLOC_H

#include<vulkan/vulkan_raii.hpp>

#include<vk_mem_alloc.h>

#include<stdexcept>
#include<utility>

// VMA and vk::raii do not compose: vk::raii::Image allocates and frees its own
// memory, while VMA wants to own it and hands back a raw VkImage plus a
// VmaAllocation. These two types fill that gap.

class Allocator {
private:
	VmaAllocator allocator = nullptr;

public:
	Allocator(const vk::raii::Instance& instance,
		const vk::raii::PhysicalDevice& physical,
		const vk::raii::Device& device) {

		const VmaAllocatorCreateInfo info{
			// Needed from Phase 3 on for acceleration structures; harmless now
			// and awkward to add later, since the allocator is built once.
			.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
			.physicalDevice = static_cast<VkPhysicalDevice>(*physical),
			.device = static_cast<VkDevice>(*device),
			.instance = static_cast<VkInstance>(*instance),
			.vulkanApiVersion = VK_API_VERSION_1_3,
		};

		if (vmaCreateAllocator(&info, &allocator) != VK_SUCCESS) {
			throw std::runtime_error("vmaCreateAllocator failed");
		}
	}

	~Allocator() {
		if (allocator) {
			vmaDestroyAllocator(allocator);
		}
	}

	Allocator(const Allocator&) = delete;
	Allocator& operator=(const Allocator&) = delete;
	Allocator(Allocator&&) = delete;
	Allocator& operator=(Allocator&&) = delete;

	VmaAllocator get() const noexcept {
		return allocator;
	}
};

// Move-only. Copying would mean two objects owning one allocation and
// vmaDestroyImage running twice on it.
class AllocatedImage {
private:
	VmaAllocator allocator = nullptr;
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = nullptr;

public:
	AllocatedImage() noexcept = default;

	AllocatedImage(const Allocator& alloc, const vk::ImageCreateInfo& info)
		:allocator(alloc.get()) {

		const VmaAllocationCreateInfo allocInfo{
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		const VkImageCreateInfo raw = static_cast<VkImageCreateInfo>(info);

		if (vmaCreateImage(allocator, &raw, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
			throw std::runtime_error("vmaCreateImage failed");
		}
	}

	~AllocatedImage() {
		// VMA documents null image and null allocation as safe; a null
		// allocator is not, so a moved-from object is guarded by this.
		if (allocator) {
			vmaDestroyImage(allocator, image, allocation);
		}
	}

	AllocatedImage(const AllocatedImage&) = delete;
	AllocatedImage& operator=(const AllocatedImage&) = delete;

	AllocatedImage(AllocatedImage&& other) noexcept
		:allocator(std::exchange(other.allocator, nullptr)),
		image(std::exchange(other.image, VK_NULL_HANDLE)),
		allocation(std::exchange(other.allocation, nullptr)) {
	}

	AllocatedImage& operator=(AllocatedImage&& other) noexcept {
		if (this != &other) {
			if (allocator) {
				vmaDestroyImage(allocator, image, allocation);
			}

			allocator = std::exchange(other.allocator, nullptr);
			image = std::exchange(other.image, VK_NULL_HANDLE);
			allocation = std::exchange(other.allocation, nullptr);
		}

		return *this;
	}

	vk::Image get() const noexcept {
		return vk::Image{ image };
	}
};

#endif
