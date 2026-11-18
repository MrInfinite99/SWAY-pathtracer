#ifndef APP_H
#define APP_H

#include<vulkan/vulkan_raii.hpp>

#include"alloc.h"

#define GLFW_INCLUDE_VULKAN
#include<GLFW/glfw3.h>
#include<array>
#include<cstdint>
#include<cstdio>
#include<cstring>
#include<algorithm>
#include<fstream>
#include<optional>
#include<stdexcept>
#include<vector>


// Owns the GLFW library itself, not any window. Must outlive every Window,
// because glfwTerminate destroys all remaining windows.
class GlfwContext {
public:
	GlfwContext() {
		if (!glfwInit()) {
			throw std::runtime_error("glfwInit failed");
		}
	}

	~GlfwContext() {
		glfwTerminate();
	}

	GlfwContext(const GlfwContext&) = delete;
	GlfwContext& operator=(const GlfwContext&) = delete;
	GlfwContext(GlfwContext&&) = delete;
	GlfwContext& operator=(GlfwContext&&) = delete;
};

class Window {
private:
	GLFWwindow* window = nullptr;
	std::uint32_t WIDTH = 0;
	std::uint32_t HEIGHT = 0;

public:
	Window() noexcept = default;
	
	// Creates the window outright: a Window either holds one or is a
	// moved-from husk. Nothing can observe it half-built.
	Window(std::uint32_t w, std::uint32_t h) :WIDTH{ w }, HEIGHT{ h } {
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
		if (!window) {
			throw std::runtime_error("glfwCreateWindow failed");
		}
	}

	~Window() {
		glfwDestroyWindow(window);
	}

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	Window(Window&& obj) noexcept {
		glfwDestroyWindow(this->window);
			this->window = obj.window;
			obj.window = nullptr;
			this->WIDTH = obj.WIDTH;
			this->HEIGHT = obj.HEIGHT;
		 
	}

	Window& operator=(Window&& obj) noexcept {
		if (this != &obj) {
			glfwDestroyWindow(this->window);
			this->window = obj.window;
			obj.window = nullptr;
			this->WIDTH = obj.WIDTH;
			this->HEIGHT = obj.HEIGHT;
		}

		return *this;
	}

	// Single-shot now: the loop lives in App, because its body needs the
	// device, swapchain and queue.
	void pollEvents() const {
		glfwPollEvents();
	}

	bool shouldClose() const {
		return glfwWindowShouldClose(window) == GLFW_TRUE;
	}

	GLFWwindow* get() const noexcept {
		return window;
	}
};

// Owns the Vulkan instance and, in debug builds, the validation layer's
// debug messenger. Needs GLFW already initialised: glfwGetRequiredInstanceExtensions
// only answers after glfwInit.
class Instance {
private:
	static constexpr const char* VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

#ifdef NDEBUG
	static constexpr bool ENABLE_VALIDATION = false;
#else
	static constexpr bool ENABLE_VALIDATION = true;
#endif

	// Destroyed in reverse order: messenger, then instance, then context.
	vk::raii::Context context;
	vk::raii::Instance instance{ nullptr };
	vk::raii::DebugUtilsMessengerEXT messenger{ nullptr };

	static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
		vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
		vk::DebugUtilsMessageTypeFlagsEXT,
		const vk::DebugUtilsMessengerCallbackDataEXT* data,
		void*) {
		if (severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
			std::fprintf(stderr, "[validation] %s\n", data->pMessage);
		}
		return VK_FALSE;
	}

	static vk::DebugUtilsMessengerCreateInfoEXT messengerInfo() {
		using Severity = vk::DebugUtilsMessageSeverityFlagBitsEXT;
		using Type = vk::DebugUtilsMessageTypeFlagBitsEXT;

		return vk::DebugUtilsMessengerCreateInfoEXT{
			.messageSeverity = Severity::eWarning | Severity::eError,
			.messageType = Type::eGeneral | Type::eValidation | Type::ePerformance,
			.pfnUserCallback = &debugCallback,
		};
	}

	bool validationAvailable() const {
		for (const auto& layer : context.enumerateInstanceLayerProperties()) {
			if (std::strcmp(layer.layerName, VALIDATION_LAYER) == 0) {
				return true;
			}
		}

		return false;
	}

public:
	Instance() {
		const bool validation = ENABLE_VALIDATION && validationAvailable();
		if (ENABLE_VALIDATION && !validation) {
			std::fprintf(stderr, "warning: %s not present, running without validation\n", VALIDATION_LAYER);
		}

		std::uint32_t glfwCount = 0;
		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
		if (!glfwExtensions) {
			throw std::runtime_error("GLFW could not supply the required instance extensions");
		}

		std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwCount);
		std::vector<const char*> layers;

		if (validation) {
			layers.push_back(VALIDATION_LAYER);
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		const vk::ApplicationInfo appInfo{
			.pApplicationName = "sway",
			.applicationVersion = 1,
			.pEngineName = "sway",
			.engineVersion = 1,
			.apiVersion = VK_API_VERSION_1_3,
		};
		const vk::DebugUtilsMessengerCreateInfoEXT debugInfo = messengerInfo();

		vk::InstanceCreateInfo createInfo{
			.pApplicationInfo = &appInfo,
			.enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
			.ppEnabledLayerNames = layers.data(),
			.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
			.ppEnabledExtensionNames = extensions.data(),
		};

		// Chained here too, so failures during instance creation are reported.
		if (validation) {
			createInfo.pNext = &debugInfo;
		}

		instance = vk::raii::Instance{ context, createInfo };

		if (validation) {
			messenger = vk::raii::DebugUtilsMessengerEXT{ instance, debugInfo };
		}
	}

	Instance(const Instance&) = delete;
	Instance& operator=(const Instance&) = delete;
	Instance(Instance&&) = delete;
	Instance& operator=(Instance&&) = delete;

	const vk::raii::Instance& get() const noexcept {
		return instance;
	}
};

// The surface belongs to the instance, not the device. GLFW creates it as a
// raw handle, which is then adopted: the RAII object owns it from here, so
// vkDestroySurfaceKHR must never be called by hand.
class Surface {
private:
	vk::raii::SurfaceKHR surface{ nullptr };

public:
	Surface(const Instance& instance, const Window& window) {
		VkSurfaceKHR raw = VK_NULL_HANDLE;

		const VkResult result = glfwCreateWindowSurface(
			static_cast<VkInstance>(*instance.get()), window.get(), nullptr, &raw);

		if (result != VK_SUCCESS) {
			throw std::runtime_error("glfwCreateWindowSurface failed");
		}

		surface = vk::raii::SurfaceKHR{ instance.get(), raw };
	}

	Surface(const Surface&) = delete;
	Surface& operator=(const Surface&) = delete;
	Surface(Surface&&) = delete;
	Surface& operator=(Surface&&) = delete;

	const vk::raii::SurfaceKHR& get() const noexcept {
		return surface;
	}
};

// Picks a physical device that can actually do ray tracing and present, then
// creates the logical device and its single queue.
class Device {
private:
	static constexpr std::array<const char*, 4> REQUIRED_EXTENSIONS = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		// Not called directly, but acceleration structures depend on it.
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	};

	// Destroyed in reverse order: queue is a borrowed handle, device last.
	vk::raii::PhysicalDevice physical{ nullptr };
	vk::raii::Device device{ nullptr };
	vk::raii::Queue queue{ nullptr };
	std::uint32_t queueFamily = 0;

	vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties{};
	vk::PhysicalDeviceAccelerationStructurePropertiesKHR asProperties{};

	static bool hasRequiredExtensions(const vk::raii::PhysicalDevice& candidate) {
		const auto available = candidate.enumerateDeviceExtensionProperties();

		for (const char* wanted : REQUIRED_EXTENSIONS) {
			bool found = false;
			for (const auto& have : available) {
				if (std::strcmp(have.extensionName, wanted) == 0) {
					found = true;
					break;
				}
			}
			if (!found) {
				return false;
			}
		}

		return true;
	}

	static bool hasRequiredFeatures(const vk::raii::PhysicalDevice& candidate) {
		const auto chain = candidate.getFeatures2<
			vk::PhysicalDeviceFeatures2,
			vk::PhysicalDeviceVulkan12Features,
			vk::PhysicalDeviceVulkan13Features,
			vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
			vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();

		return chain.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress &&
			chain.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
			chain.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
			chain.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure &&
			chain.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>().rayTracingPipeline;
	}

	// Presentation support is asked of GLFW, not of a surface: no surface
	// exists yet. The surface-specific check comes with the swapchain.
	static std::optional<std::uint32_t> findQueueFamily(
		const vk::raii::Instance& instance,
		const vk::raii::PhysicalDevice& candidate) {

		const auto families = candidate.getQueueFamilyProperties();

		for (std::uint32_t i = 0; i < families.size(); ++i) {
			const bool graphics = static_cast<bool>(families[i].queueFlags & vk::QueueFlagBits::eGraphics);
			const bool present = glfwGetPhysicalDevicePresentationSupport(
				static_cast<VkInstance>(*instance), static_cast<VkPhysicalDevice>(*candidate), i) == GLFW_TRUE;

			if (graphics && present) {
				return i;
			}
		}

		return std::nullopt;
	}

public:
	explicit Device(const Instance& inst) {
		const vk::raii::Instance& handle = inst.get();

		std::optional<std::uint32_t> chosenFamily;

		for (const auto& candidate : handle.enumeratePhysicalDevices()) {
			if (!hasRequiredExtensions(candidate) || !hasRequiredFeatures(candidate)) {
				continue;
			}

			const auto family = findQueueFamily(handle, candidate);
			if (!family) {
				continue;
			}

			const bool discrete =
				candidate.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu;

			// Take the first usable device, but keep looking for a discrete one:
			// the integrated GPU here also advertises ray tracing.
			if (!*physical || discrete) {
				physical = candidate;
				chosenFamily = family;
			}

			if (discrete) {
				break;
			}
		}

		if (!*physical) {
			throw std::runtime_error("no Vulkan device supports ray tracing, presentation and Vulkan 1.3");
		}

		queueFamily = *chosenFamily;

		const auto properties = physical.getProperties2<
			vk::PhysicalDeviceProperties2,
			vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
			vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();

		rtProperties = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
		asProperties = properties.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();

		const float priority = 1.0f;
		const vk::DeviceQueueCreateInfo queueInfo{
			.queueFamilyIndex = queueFamily,
			.queueCount = 1,
			.pQueuePriorities = &priority,
		};

		vk::StructureChain<
			vk::DeviceCreateInfo,
			vk::PhysicalDeviceVulkan12Features,
			vk::PhysicalDeviceVulkan13Features,
			vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
			vk::PhysicalDeviceRayTracingPipelineFeaturesKHR> chain{
				vk::DeviceCreateInfo{
					.queueCreateInfoCount = 1,
					.pQueueCreateInfos = &queueInfo,
					.enabledExtensionCount = static_cast<std::uint32_t>(REQUIRED_EXTENSIONS.size()),
					.ppEnabledExtensionNames = REQUIRED_EXTENSIONS.data(),
				},
				vk::PhysicalDeviceVulkan12Features{ .bufferDeviceAddress = vk::True },
				vk::PhysicalDeviceVulkan13Features{ .synchronization2 = vk::True, .dynamicRendering = vk::True },
				vk::PhysicalDeviceAccelerationStructureFeaturesKHR{ .accelerationStructure = vk::True },
				vk::PhysicalDeviceRayTracingPipelineFeaturesKHR{ .rayTracingPipeline = vk::True },
		};

		device = vk::raii::Device{ physical, chain.get<vk::DeviceCreateInfo>() };
		queue = vk::raii::Queue{ device, queueFamily, 0 };

		const auto name = physical.getProperties().deviceName;
		std::printf("device: %s (queue family %u)\n", name.data(), queueFamily);
		std::printf("  shaderGroupHandleSize      %u\n", rtProperties.shaderGroupHandleSize);
		std::printf("  shaderGroupHandleAlignment %u\n", rtProperties.shaderGroupHandleAlignment);
		std::printf("  shaderGroupBaseAlignment   %u\n", rtProperties.shaderGroupBaseAlignment);
		std::printf("  maxRayRecursionDepth       %u\n", rtProperties.maxRayRecursionDepth);
		std::printf("  scratchOffsetAlignment     %u\n",
			asProperties.minAccelerationStructureScratchOffsetAlignment);
	}

	Device(const Device&) = delete;
	Device& operator=(const Device&) = delete;
	Device(Device&&) = delete;
	Device& operator=(Device&&) = delete;

	const vk::raii::PhysicalDevice& getPhysical() const noexcept { return physical; }
	const vk::raii::Device& get() const noexcept { return device; }
	const vk::raii::Queue& getQueue() const noexcept { return queue; }
	std::uint32_t getQueueFamily() const noexcept { return queueFamily; }

	const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& getRtProperties() const noexcept {
		return rtProperties;
	}

	const vk::PhysicalDeviceAccelerationStructurePropertiesKHR& getAsProperties() const noexcept {
		return asProperties;
	}
};

// Swapchain images are owned by the swapchain and are never created or
// destroyed by us. Usage includes eTransferDst, not just eColorAttachment,
// because nothing here renders into them with a graphics pipeline: Phase 1
// clears them and Phase 2 onward blits into them, both transfer operations.
class Swapchain {
private:
	vk::raii::SwapchainKHR swapchain{ nullptr };
	std::vector<vk::Image> images;
	vk::Format format = vk::Format::eUndefined;
	vk::Extent2D extent{};

	static constexpr vk::ImageUsageFlags USAGE =
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;

	static vk::SurfaceFormatKHR chooseFormat(const std::vector<vk::SurfaceFormatKHR>& available) {
		for (const auto& candidate : available) {
			if (candidate.format == vk::Format::eB8G8R8A8Unorm &&
				candidate.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
				return candidate;
			}
		}

		return available.front();
	}

	static vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& caps, const Window& window) {
		// A currentExtent of UINT32_MAX means the surface lets us choose.
		if (caps.currentExtent.width != UINT32_MAX) {
			return caps.currentExtent;
		}

		int w = 0;
		int h = 0;
		glfwGetFramebufferSize(window.get(), &w, &h);

		return vk::Extent2D{
			std::clamp(static_cast<std::uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width),
			std::clamp(static_cast<std::uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height),
		};
	}

public:
	Swapchain(const Device& device, const Surface& surface, const Window& window) {
		const vk::raii::PhysicalDevice& physical = device.getPhysical();
		const vk::SurfaceKHR handle = *surface.get();

		// GLFW said this queue family can present to the platform; this asks
		// whether it can present to this particular surface.
		if (!physical.getSurfaceSupportKHR(device.getQueueFamily(), handle)) {
			throw std::runtime_error("chosen queue family cannot present to this surface");
		}

		const vk::SurfaceCapabilitiesKHR caps = physical.getSurfaceCapabilitiesKHR(handle);

		if ((caps.supportedUsageFlags & USAGE) != USAGE) {
			throw std::runtime_error("surface does not support transfer-dst swapchain images");
		}

		const vk::SurfaceFormatKHR surfaceFormat = chooseFormat(physical.getSurfaceFormatsKHR(handle));
		format = surfaceFormat.format;
		extent = chooseExtent(caps, window);

		std::uint32_t imageCount = caps.minImageCount + 1;
		if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
			imageCount = caps.maxImageCount;
		}

		const vk::SwapchainCreateInfoKHR createInfo{
			.surface = handle,
			.minImageCount = imageCount,
			.imageFormat = format,
			.imageColorSpace = surfaceFormat.colorSpace,
			.imageExtent = extent,
			.imageArrayLayers = 1,
			.imageUsage = USAGE,
			.imageSharingMode = vk::SharingMode::eExclusive,
			.preTransform = caps.currentTransform,
			.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
			// eFifo is the only present mode guaranteed to exist, and it
			// paces the frame loop on vsync.
			.presentMode = vk::PresentModeKHR::eFifo,
			.clipped = vk::True,
		};

		swapchain = vk::raii::SwapchainKHR{ device.get(), createInfo };
		images = swapchain.getImages();

		std::printf("swapchain: %zu images, %ux%u, format %s\n",
			images.size(), extent.width, extent.height, vk::to_string(format).c_str());
	}

	Swapchain(const Swapchain&) = delete;
	Swapchain& operator=(const Swapchain&) = delete;
	Swapchain(Swapchain&&) = delete;
	Swapchain& operator=(Swapchain&&) = delete;

	const vk::raii::SwapchainKHR& get() const noexcept { return swapchain; }
	const std::vector<vk::Image>& getImages() const noexcept { return images; }
	vk::Format getFormat() const noexcept { return format; }
	vk::Extent2D getExtent() const noexcept { return extent; }
};

inline std::vector<std::uint32_t> readSpirv(const char* path) {
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	if (!file) {
		throw std::runtime_error(std::string{ "cannot open " } + path);
	}

	const std::streamsize bytes = file.tellg();
	if (bytes <= 0 || bytes % 4 != 0) {
		throw std::runtime_error(std::string{ "not valid SPIR-V: " } + path);
	}

	// Read into uint32_t, not char: pCode wants 4-byte-aligned words.
	std::vector<std::uint32_t> code(static_cast<std::size_t>(bytes) / 4);
	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), bytes);

	return code;
}

// The image the renderer actually draws into. Float, because accumulation
// from Phase 6 on needs more precision than the swapchain's 8-bit UNORM, and
// storage-image usage on swapchain images is not reliably supported anyway.
class StorageImage {
public:
	static constexpr vk::Format FORMAT = vk::Format::eR32G32B32A32Sfloat;

private:
	AllocatedImage image;
	vk::raii::ImageView view{ nullptr };
	vk::Extent2D extent{};

public:
	StorageImage(const Device& device, const Allocator& allocator, vk::Extent2D size)
		:extent(size) {

		image = AllocatedImage{ allocator, vk::ImageCreateInfo{
			.imageType = vk::ImageType::e2D,
			.format = FORMAT,
			.extent = vk::Extent3D{ size.width, size.height, 1 },
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = vk::SampleCountFlagBits::e1,
			.tiling = vk::ImageTiling::eOptimal,
			.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
			.sharingMode = vk::SharingMode::eExclusive,
			.initialLayout = vk::ImageLayout::eUndefined,
		} };

		view = vk::raii::ImageView{ device.get(), vk::ImageViewCreateInfo{
			.image = image.get(),
			.viewType = vk::ImageViewType::e2D,
			.format = FORMAT,
			.subresourceRange = vk::ImageSubresourceRange{
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		} };
	}

	StorageImage(const StorageImage&) = delete;
	StorageImage& operator=(const StorageImage&) = delete;
	StorageImage(StorageImage&&) = delete;
	StorageImage& operator=(StorageImage&&) = delete;

	vk::Image get() const noexcept { return image.get(); }
	const vk::raii::ImageView& getView() const noexcept { return view; }
	vk::Extent2D getExtent() const noexcept { return extent; }
};

// Throwaway, deleted at Phase 4. It exists so that when ray tracing renders
// black, the image path is already known to work.
class GradientPipeline {
private:
	vk::raii::DescriptorSetLayout setLayout{ nullptr };
	vk::raii::DescriptorPool descriptorPool{ nullptr };
	std::vector<vk::raii::DescriptorSet> sets;
	vk::raii::PipelineLayout pipelineLayout{ nullptr };
	vk::raii::Pipeline pipeline{ nullptr };

public:
	GradientPipeline(const Device& device, const StorageImage& target) {
		const vk::DescriptorSetLayoutBinding binding{
			.binding = 0,
			.descriptorType = vk::DescriptorType::eStorageImage,
			.descriptorCount = 1,
			.stageFlags = vk::ShaderStageFlagBits::eCompute,
		};

		setLayout = vk::raii::DescriptorSetLayout{ device.get(), vk::DescriptorSetLayoutCreateInfo{
			.bindingCount = 1,
			.pBindings = &binding,
		} };

		const vk::DescriptorPoolSize poolSize{
			.type = vk::DescriptorType::eStorageImage,
			.descriptorCount = 1,
		};

		descriptorPool = vk::raii::DescriptorPool{ device.get(), vk::DescriptorPoolCreateInfo{
			// raii frees the set in its destructor, which the pool must allow.
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = 1,
			.poolSizeCount = 1,
			.pPoolSizes = &poolSize,
		} };

		const vk::DescriptorSetLayout rawLayout = *setLayout;

		sets = vk::raii::DescriptorSets{ device.get(), vk::DescriptorSetAllocateInfo{
			.descriptorPool = *descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &rawLayout,
		} };

		const vk::DescriptorImageInfo imageInfo{
			.imageView = *target.getView(),
			// Storage images are written in eGeneral; there is no
			// "optimal for writing" layout for them.
			.imageLayout = vk::ImageLayout::eGeneral,
		};

		device.get().updateDescriptorSets(vk::WriteDescriptorSet{
			.dstSet = *sets.front(),
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eStorageImage,
			.pImageInfo = &imageInfo,
		}, nullptr);

		pipelineLayout = vk::raii::PipelineLayout{ device.get(), vk::PipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &rawLayout,
		} };

		const std::vector<std::uint32_t> code = readSpirv("shaders/gradient.spv");

		const vk::raii::ShaderModule module{ device.get(), vk::ShaderModuleCreateInfo{
			.codeSize = code.size() * sizeof(std::uint32_t),
			.pCode = code.data(),
		} };

		pipeline = vk::raii::Pipeline{ device.get(), nullptr, vk::ComputePipelineCreateInfo{
			.stage = vk::PipelineShaderStageCreateInfo{
				.stage = vk::ShaderStageFlagBits::eCompute,
				.module = *module,
				// Must match the name in the SPIR-V, which slangc kept
				// because of -fvk-use-entrypoint-name.
				.pName = "computeMain",
			},
			.layout = *pipelineLayout,
		} };
	}

	GradientPipeline(const GradientPipeline&) = delete;
	GradientPipeline& operator=(const GradientPipeline&) = delete;
	GradientPipeline(GradientPipeline&&) = delete;
	GradientPipeline& operator=(GradientPipeline&&) = delete;

	const vk::raii::Pipeline& get() const noexcept { return pipeline; }
	const vk::raii::PipelineLayout& getLayout() const noexcept { return pipelineLayout; }
	vk::DescriptorSet getSet() const noexcept { return *sets.front(); }
};

// One frame: wait for this frame's fence, acquire an image, clear it, present.
// No pipeline is involved - the clear is a transfer operation, and the only
// real work is moving the image between layouts.
class Renderer {
private:
	static constexpr std::size_t MAX_FRAMES_IN_FLIGHT = 2;

	const Device& device;
	const Swapchain& swapchain;
	const StorageImage& storageImage;
	const GradientPipeline& gradient;

	vk::raii::CommandPool pool{ nullptr };
	std::vector<vk::raii::CommandBuffer> commandBuffers;

	// Per frame in flight: these are waited on before the frame is reused.
	std::vector<vk::raii::Semaphore> imageAvailable;
	std::vector<vk::raii::Fence> inFlight;

	// Per swapchain image, not per frame: presentation waits on this, and
	// which image we get back from acquire is the driver's choice.
	std::vector<vk::raii::Semaphore> renderFinished;

	std::size_t frame = 0;

	static constexpr vk::ImageSubresourceRange COLOR_RANGE{
		.aspectMask = vk::ImageAspectFlagBits::eColor,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};

	static vk::ImageMemoryBarrier2 barrier(
		vk::Image image,
		vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess, vk::ImageLayout oldLayout,
		vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess, vk::ImageLayout newLayout) {

		return vk::ImageMemoryBarrier2{
			.srcStageMask = srcStage,
			.srcAccessMask = srcAccess,
			.dstStageMask = dstStage,
			.dstAccessMask = dstAccess,
			.oldLayout = oldLayout,
			.newLayout = newLayout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = COLOR_RANGE,
		};
	}

	void record(const vk::raii::CommandBuffer& cmd, vk::Image swapImage) const {
		cmd.reset();
		cmd.begin(vk::CommandBufferBeginInfo{
			.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
		});

		const vk::Image target = storageImage.get();
		const vk::Extent2D size = storageImage.getExtent();

		// eUndefined every frame: the shader writes every pixel, so last
		// frame's contents are not worth preserving. That changes at Phase 6,
		// where accumulation reads the previous result back.
		const vk::ImageMemoryBarrier2 toGeneral = barrier(target,
			vk::PipelineStageFlagBits2::eAllCommands, {}, vk::ImageLayout::eUndefined,
			vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
			vk::ImageLayout::eGeneral);

		cmd.pipelineBarrier2(vk::DependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &toGeneral,
		});

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *gradient.get());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *gradient.getLayout(),
			0, gradient.getSet(), nullptr);

		// Rounded up: the shader discards threads past the edge.
		cmd.dispatch((size.width + 7) / 8, (size.height + 7) / 8, 1);

		const std::array<vk::ImageMemoryBarrier2, 2> beforeBlit{
			barrier(target,
				vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
				vk::ImageLayout::eGeneral,
				vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
				vk::ImageLayout::eTransferSrcOptimal),
			barrier(swapImage,
				vk::PipelineStageFlagBits2::eAllCommands, {}, vk::ImageLayout::eUndefined,
				vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
				vk::ImageLayout::eTransferDstOptimal),
		};

		cmd.pipelineBarrier2(vk::DependencyInfo{
			.imageMemoryBarrierCount = static_cast<std::uint32_t>(beforeBlit.size()),
			.pImageMemoryBarriers = beforeBlit.data(),
		});

		const vk::ImageSubresourceLayers layers{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};

		vk::ImageBlit region{};
		region.srcSubresource = layers;
		region.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
		region.srcOffsets[1] = vk::Offset3D{ static_cast<std::int32_t>(size.width),
			static_cast<std::int32_t>(size.height), 1 };
		region.dstSubresource = layers;
		region.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
		region.dstOffsets[1] = vk::Offset3D{ static_cast<std::int32_t>(swapchain.getExtent().width),
			static_cast<std::int32_t>(swapchain.getExtent().height), 1 };

		// The blit also converts R32G32B32A32_SFLOAT down to the swapchain's
		// B8G8R8A8_UNORM. Nearest because the extents match exactly.
		cmd.blitImage(target, vk::ImageLayout::eTransferSrcOptimal,
			swapImage, vk::ImageLayout::eTransferDstOptimal, region, vk::Filter::eNearest);

		const vk::ImageMemoryBarrier2 toPresent = barrier(swapImage,
			vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
			vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits2::eAllCommands, {}, vk::ImageLayout::ePresentSrcKHR);

		cmd.pipelineBarrier2(vk::DependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &toPresent,
		});

		cmd.end();
	}

public:
	Renderer(const Device& dev, const Swapchain& swap, const StorageImage& target, const GradientPipeline& grad)
		:device(dev), swapchain(swap), storageImage(target), gradient(grad) {
		pool = vk::raii::CommandPool{ device.get(), vk::CommandPoolCreateInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = device.getQueueFamily(),
		} };

		commandBuffers = vk::raii::CommandBuffers{ device.get(), vk::CommandBufferAllocateInfo{
			.commandPool = *pool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = static_cast<std::uint32_t>(MAX_FRAMES_IN_FLIGHT),
		} };

		for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			imageAvailable.emplace_back(device.get(), vk::SemaphoreCreateInfo{});

			// Created signalled, so the first wait on it returns at once.
			inFlight.emplace_back(device.get(), vk::FenceCreateInfo{
				.flags = vk::FenceCreateFlagBits::eSignaled,
			});
		}

		for (std::size_t i = 0; i < swapchain.getImages().size(); ++i) {
			renderFinished.emplace_back(device.get(), vk::SemaphoreCreateInfo{});
		}
	}

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;
	Renderer(Renderer&&) = delete;
	Renderer& operator=(Renderer&&) = delete;

	void drawFrame() {
		const vk::raii::Fence& fence = inFlight[frame];

		while (device.get().waitForFences(*fence, vk::True, UINT64_MAX) == vk::Result::eTimeout) {
		}

		std::uint32_t imageIndex = 0;

		try {
			// raii reports out-of-date by throwing, not by a result code.
			const auto [result, index] = swapchain.get().acquireNextImage(UINT64_MAX, *imageAvailable[frame]);
			static_cast<void>(result);
			imageIndex = index;
		}
		catch (const vk::OutOfDateKHRError&) {
			return;
		}

		// Reset only once we know we are going to submit, or a skipped frame
		// leaves the fence unsignalled and the next wait hangs forever.
		device.get().resetFences(*fence);

		record(commandBuffers[frame], swapchain.getImages()[imageIndex]);

		const vk::SemaphoreSubmitInfo waitInfo{
			.semaphore = *imageAvailable[frame],
			.value = 0,
			.stageMask = vk::PipelineStageFlagBits2::eComputeShader,
			.deviceIndex = 0,
		};

		const vk::CommandBufferSubmitInfo commandInfo{
			.commandBuffer = *commandBuffers[frame],
			.deviceMask = 0,
		};

		const vk::SemaphoreSubmitInfo signalInfo{
			.semaphore = *renderFinished[imageIndex],
			.value = 0,
			.stageMask = vk::PipelineStageFlagBits2::eAllCommands,
			.deviceIndex = 0,
		};

		device.getQueue().submit2(vk::SubmitInfo2{
			.waitSemaphoreInfoCount = 1,
			.pWaitSemaphoreInfos = &waitInfo,
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &commandInfo,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos = &signalInfo,
		}, *fence);

		const vk::Semaphore waitOn = *renderFinished[imageIndex];
		const vk::SwapchainKHR handle = *swapchain.get();

		try {
			static_cast<void>(device.getQueue().presentKHR(vk::PresentInfoKHR{
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = &waitOn,
				.swapchainCount = 1,
				.pSwapchains = &handle,
				.pImageIndices = &imageIndex,
			}));
		}
		catch (const vk::OutOfDateKHRError&) {
		}

		frame = (frame + 1) % MAX_FRAMES_IN_FLIGHT;
	}
};

class App {
private:

	GlfwContext glfw;
	Window window;
	// After glfw: needs it initialised, and must be destroyed before it.
	Instance instance;
	// After instance, before device: the surface is a child of the instance,
	// and the swapchain that uses it must die before it does.
	Surface surface;
	// After instance: destroyed before it, as a device is a child of it.
	Device device;
	// Last: destroyed first, before both the device and the surface.
	Swapchain swapchain;
	// After the device: frees its allocations through it.
	Allocator allocator;
	// Sized to the swapchain, so after it; destroyed before the allocator.
	StorageImage storageImage;
	GradientPipeline gradient;
	// After the swapchain: its semaphores are sized from the image count.
	Renderer renderer;
public:
	App(std::uint32_t w, std::uint32_t h)
		:window(w, h),
		surface(instance, window),
		device(instance),
		swapchain(device, surface, window),
		allocator(instance.get(), device.getPhysical(), device.get()),
		storageImage(device, allocator, swapchain.getExtent()),
		gradient(device, storageImage),
		renderer(device, swapchain, storageImage, gradient) {

	}

	App(const App&) = delete;
	App(App&&) = delete;
	App& operator=(const App&) = delete;
	App& operator=(App&&) = delete;

	~App() = default;

	void run() {
		while (!window.shouldClose()) {
			window.pollEvents();
			renderer.drawFrame();
		}

		// Nothing may be in flight when the members below start destroying
		// semaphores, fences and command buffers.
		device.get().waitIdle();
	}

	 
};


#endif