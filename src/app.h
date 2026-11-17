#ifndef APP_H
#define APP_H

#include<vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_VULKAN
#include<GLFW/glfw3.h>
#include<cstdint>
#include<cstdio>
#include<cstring>
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
	
	Window(std::uint32_t w, std::uint32_t h) :WIDTH{ w }, HEIGHT{ h } {

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

	void initWindow() {
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
	}

	void pollEvents() {
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
		}
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

class App {
private:

	GlfwContext glfw;
	Window window;
	// After glfw: needs it initialised, and must be destroyed before it.
	Instance instance;
public:
	App(std::uint32_t w, std::uint32_t h) :window(w, h) {

	}

	App(const App&) = delete;
	App(App&&) = delete;
	App& operator=(const App&) = delete;
	App& operator=(App&&) = delete;

	~App() = default;

	void run() {
	 
		window.initWindow();
		window.pollEvents();
		 

	}

	 
};


#endif