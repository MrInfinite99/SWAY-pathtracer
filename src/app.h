#ifndef APP_H
#define APP_H

#include<vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_VULKAN
#include<GLFW/glfw3.h>
#include<array>
#include<cstdint>
#include<cstdio>
#include<cstring>
#include<algorithm>
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

	void pollEvents() {
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
		}
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
public:
	App(std::uint32_t w, std::uint32_t h)
		:window(w, h),
		surface(instance, window),
		device(instance),
		swapchain(device, surface, window) {

	}

	App(const App&) = delete;
	App(App&&) = delete;
	App& operator=(const App&) = delete;
	App& operator=(App&&) = delete;

	~App() = default;

	void run() {
		window.pollEvents();
	}

	 
};


#endif