#include "Engine.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <cassert>


Engine::Engine() : m_Running(false), m_Window(nullptr)
{
	glm::vec3 randomVec3;
}

bool Engine::Init(const char* title, int width, int height)
{
	if(SDL_InitSubSystem(SDL_INIT_VIDEO))
	{
		m_Window = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

		if(!m_Window)
		{
			SDL_Log("Failed to create window: %s", SDL_GetError());
			return false;
		}

		if(!InitializeVulkan())
		{

		}
	}

	m_Running = true;
	return true;
}

void Engine::Run()
{
	assert(m_Running, "Engine is not running. Call Init() before Run().");
	while(m_Running)
	{
		SDL_Event event{ 0 };
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT)
			{
				m_Running = false;
				break;
			}
			else if (event.type == SDL_EVENT_WINDOW_RESIZED)
			{

			}
		}
	}
}

void Engine::Shutdown()
{
	DestroySwapchain();

	if(m_VmaAllocator)
	{
		vmaDestroyAllocator(m_VmaAllocator);
	}

	if(m_VulkanSurface)
	{
		vkDestroySurfaceKHR(m_VulkanInstance, m_VulkanSurface, nullptr);
	}

	if (m_LogicalDevice)
	{
		vkDestroyDevice(m_LogicalDevice, nullptr);
	}

	if(m_VulkanInstance)
	{
		vkDestroyInstance(m_VulkanInstance, nullptr);
	}
	volkFinalize();

	if(m_Window)
	{
		SDL_DestroyWindow(m_Window);
	}
	SDL_Quit();
}

bool Engine::InitializeVulkan()
{
	if(!CreateVulkanInstance())
	{
		SDL_Log("Failed to create Vulkan instance.");
		return false;
	}

	if(!CreateVulkanSurface())
	{
		SDL_Log("Failed to create Vulkan surface.");
		return false;
	}

	if (m_PhysicalDevice = FindPhysicalDevice(); !m_PhysicalDevice) 
	{
		SDL_Log("Failed to find a suitable physical device.");
		return false;
	}

	if(!FindGraphicsQueue())
	{
		SDL_Log("Failed to find a suitable graphics queue.");
		return false;
	}

	if(!CreateLogicalDevice())
	{
		SDL_Log("Failed to create logical device.");
		return false;
	}

	if (!InitializeVMA()) 
	{
		SDL_Log("Failed to create VMA");
		return false;
	}

	if(!CreateSwapchain(1920, 1080))
	{
		SDL_Log("Unable to create swapchain");
		return false;
	}

	if(!CreateShaders())
	{
		SDL_Log("Error creating shader modules");
		return false;
	}
}

bool Engine::CreateVulkanInstance()
{
	if(volkInitialize() != VK_SUCCESS)
	{
		SDL_Log("Failed to initialize Volk.");
		return false;
	}

	// Vulkan instance creation code goes here
	VkApplicationInfo appInfo
	{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Vulkan Engine",
		.apiVersion = s_VulkanVersion
	};

	uint32_t instExtCount = 0;
	const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&instExtCount);

	std::vector<const char*> requestedExtensions{ VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

	for (int i = 0; i < instExtCount; ++i)
	{
		requestedExtensions.push_back(extensions[i]);
	}

	std::vector<const char*> requestedLayers{ "VK_LAYER_KHRONOS_validation" };

	VkDebugUtilsMessengerCreateInfoEXT debugInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debugCallback
	};

	VkInstanceCreateInfo createInfo
	{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = &debugInfo,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = static_cast<uint32_t>(requestedLayers.size()),
		.ppEnabledLayerNames = requestedLayers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
		.ppEnabledExtensionNames = requestedExtensions.data(),
	};

	if (vkCreateInstance(&createInfo, nullptr, &m_VulkanInstance) != VK_SUCCESS)
	{
		return false;
	}

	volkLoadInstance(m_VulkanInstance);
}

bool Engine::CreateVulkanSurface()
{
	if(!SDL_Vulkan_CreateSurface(m_Window, m_VulkanInstance, nullptr, &m_VulkanSurface))
	{
		SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
		return false;
	}
	return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL Engine::debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		SDL_Log("Vulkan validation layer: %s", pCallbackData->pMessage);
	}

	return VK_FALSE;
}

VkPhysicalDevice Engine::FindPhysicalDevice()
{
	uint32_t deviceCount = 0;

	vkEnumeratePhysicalDevices(m_VulkanInstance, &deviceCount, nullptr);
	std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
	vkEnumeratePhysicalDevices(m_VulkanInstance, &deviceCount, physicalDevices.data());

	VkPhysicalDevice physicalDevice = nullptr;

	if(deviceCount)
	{
		physicalDevice = physicalDevices[0];

		for(const auto& pDevice : physicalDevices)
		{
			VkPhysicalDeviceProperties deviceProperties{};
			vkGetPhysicalDeviceProperties(pDevice, &deviceProperties);
			if(deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				physicalDevice = pDevice;
				break;
			}
		}
	}

	// Ensure the desired swapchain format is supported
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_VulkanSurface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_VulkanSurface, &formatCount, surfaceFormats.data());

	bool formatSupported = false;
	for (const VkSurfaceFormatKHR& surfFormat : surfaceFormats)
	{
		if(surfFormat.format == s_SwapchainFormat)
		{
			formatSupported = true;
			break;
		}
	}

	if(!formatSupported)
	{
		SDL_Log("Desired swapchain format is not supported by the physical device.");
		return nullptr;
	}

	return physicalDevice;
}

bool Engine::FindGraphicsQueue() 
{
	uint32_t queueFamCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties2(m_PhysicalDevice, &queueFamCount, nullptr);
	std::vector<VkQueueFamilyProperties2> queueFamProps(queueFamCount, { .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
	vkGetPhysicalDeviceQueueFamilyProperties2(m_PhysicalDevice, &queueFamCount, queueFamProps.data());

	for(int currentFamIdx = 0; currentFamIdx< queueFamCount; ++currentFamIdx)
	{
		VkBool32 presentSupport = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, currentFamIdx, m_VulkanSurface, &presentSupport);

		const auto& props = queueFamProps[currentFamIdx];

		if(props.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport)
		{
			m_GfxQueueFamIdx = currentFamIdx;
			return true;
		}
	}
}

bool Engine::CreateLogicalDevice() 
{
	VkPhysicalDeviceVulkan14Features supportedFeatures14
	{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext = nullptr };
	VkPhysicalDeviceVulkan13Features supportedFeatures13
	{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &supportedFeatures14 };
	VkPhysicalDeviceVulkan12Features supportedFeatures12
	{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supportedFeatures13 };
	VkPhysicalDeviceFeatures2 supportedFeatures
	{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supportedFeatures12 };

	vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &supportedFeatures);

	// Check if what we need is supported
	if(!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2 || !supportedFeatures12.timelineSemaphore)
	{
		SDL_Log("Required Vulkan features are not supported.");
		return false;
	}

	VkPhysicalDeviceVulkan14Features requestedFeatures14
	{ 
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = nullptr
	};
	VkPhysicalDeviceVulkan13Features requestedFeatures13
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &requestedFeatures14,
		.synchronization2 = VK_TRUE,
		.dynamicRendering = VK_TRUE
	};
	VkPhysicalDeviceVulkan12Features requestedFeatures12
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &requestedFeatures13,
		.timelineSemaphore = VK_TRUE
	};

	VkPhysicalDeviceFeatures2 requestedFeatures
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &requestedFeatures12
	};

	// Request the queue we will be using
	std::vector<float> queuePriorities{ 1.0f };
	VkDeviceQueueCreateInfo gfxQueueInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = m_GfxQueueFamIdx,
		.queueCount = 1,
		.pQueuePriorities = queuePriorities.data()
	};

	// Request the extensions we will be using
	const std::vector<const char*> requestedExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo deviceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &requestedFeatures,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &gfxQueueInfo,
		.enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
		.ppEnabledExtensionNames = requestedExtensions.data(),
		.pEnabledFeatures = nullptr // Deprecated, use pNext chain instead
	};

	if(vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, &m_LogicalDevice) != VK_SUCCESS)
	{
		SDL_Log("Failed to create logical device.");
		return false;
	}

	vkGetDeviceQueue(m_LogicalDevice, m_GfxQueueFamIdx, 0, &m_GfxQueue);
	if(!m_GfxQueue)
	{
		SDL_Log("Failed to retrieve graphics queue.");
		return false;
	}

	return true;
}

bool Engine::InitializeVMA() 
{
	VmaVulkanFunctions vmaFuncInfo{};
	VmaAllocatorCreateInfo vmaAllocCreateInfo
	{
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.physicalDevice = m_PhysicalDevice,
		.device = m_LogicalDevice,
		.pVulkanFunctions = &vmaFuncInfo,
		.instance = m_VulkanInstance,
		.vulkanApiVersion = s_VulkanVersion
	};

	vmaImportVulkanFunctionsFromVolk(&vmaAllocCreateInfo, &vmaFuncInfo);

	if(vmaCreateAllocator(&vmaAllocCreateInfo, &m_VmaAllocator) != VK_SUCCESS)
	{
		SDL_Log("Failed to create VMA allocator.");
		return false;
	}

	return true;
}

bool Engine::CreateSwapchain(uint32_t width, uint32_t height) 
{
	// Swapchain creation code goes here
	m_SwapchainWidth = width;
	m_SwapchainHeight = height;

	VkSurfaceCapabilitiesKHR surfaceCapabilities{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_VulkanSurface, &surfaceCapabilities) != VK_SUCCESS)
	{
		SDL_Log("Failed to get surface capabilities.");
		return false;
	}

	uint32_t requestedImageCount = std::max(2u, surfaceCapabilities.minImageCount);
	if(surfaceCapabilities.maxImageCount > 0)
	{
		requestedImageCount = std::min(requestedImageCount, surfaceCapabilities.maxImageCount);
	}

	VkSwapchainCreateInfoKHR swapchainCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = m_VulkanSurface,
		.minImageCount = requestedImageCount,
		.imageFormat = s_SwapchainFormat,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = { .width = m_SwapchainWidth, .height = m_SwapchainHeight },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = surfaceCapabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
	};

	if(vkCreateSwapchainKHR(m_LogicalDevice, &swapchainCreateInfo, nullptr, &m_Swapchain) != VK_SUCCESS)
	{
		SDL_Log("Failed to create swapchain.");
		return false;
	}

	uint32_t swapchainImageCount = 0;
	vkGetSwapchainImagesKHR(m_LogicalDevice, m_Swapchain, &swapchainImageCount, nullptr);
	m_SwapchainImages.resize(swapchainImageCount);
	vkGetSwapchainImagesKHR(m_LogicalDevice, m_Swapchain, &swapchainImageCount, m_SwapchainImages.data());
	m_SwapchainImageViews.resize(swapchainImageCount);

	for (size_t i = 0; i < m_SwapchainImages.size(); ++i)
	{
		VkImageViewCreateInfo imgViewInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = m_SwapchainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = s_SwapchainFormat,
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		if(vkCreateImageView(m_LogicalDevice, &imgViewInfo, nullptr, &m_SwapchainImageViews[i]) != VK_SUCCESS)
		{
			SDL_Log("Failed to create image view for swapchain image %zu.", i);
			return false;
		}
	}

	m_RenderCompleteSemaphores.resize(m_SwapchainImages.size());
	for(VkSemaphore& semaphore : m_RenderCompleteSemaphores)
	{
		VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		if(vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
		{
			SDL_Log("Error creating the render-complete semaphore");
			return false;
		}
	}

	// Create depth image
	VkImageCreateInfo depthCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = s_DepthFormat,
		.extent { .width = m_SwapchainWidth, .height = m_SwapchainHeight, .depth = 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VmaAllocationCreateInfo allocInfo
	{
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO
	};

	if(vmaCreateImage(m_VmaAllocator, &depthCreateInfo, &allocInfo, &m_DepthImage, &m_DepthImageAllocation, nullptr) != VK_SUCCESS)
	{
		SDL_Log("Error allocating depth image");
		return false;
	}

	VkImageViewCreateInfo depthImgViewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = m_DepthImage,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = s_DepthFormat,
		.subresourceRange {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
	};

	if(vkCreateImageView(m_LogicalDevice, &depthImgViewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS)
	{
		SDL_Log("Error creating depth image view");
		return false;
	}

	return true;
}

void Engine::DestroySwapchain()
{
	for(VkImageView& swapchainImgView : m_SwapchainImageViews)
	{
		vkDestroyImageView(m_Device, swapchainImgView, nullptr);
	}
	m_SwapchainImageViews.clear();

	for(VkSemaphore& semaphore : m_RenderCompleteSemaphores)
	{
		vkDestroySemaphore(m_Device, semaphore, nullptr);
	}
	m_RenderCompleteSemaphores.clear();

	if(m_Swapchain)
	{
		vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
		m_Swapchain = nullptr;
	}

	if(m_DepthImageView)
	{
		vkDestroyImageView(m_Device, m_DepthImageView, nullptr);
		vmaDestroyImage(m_VmaAllocator, m_DepthImage, m_DepthImageAllocation);
		m_DepthImageView = nullptr;
	}
}

bool Engine::CreateShaders()
{
	return true;
}