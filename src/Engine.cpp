#include "Engine.h"

#include <iostream>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <fstream>
#include <sstream>

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
			Render();
		}
	}
}

void Engine::Shutdown()
{
	// Wait in case resources are in use
	vkDeviceWaitIdle(m_LogicalDevice);

	// Frame / sync object cleanup
	if(m_TimelineSemaphore)
	{
		vkDestroySemaphore(m_LogicalDevice, m_TimelineSemaphore, nullptr);
	}
	for(auto& res : m_FrameResources)
	{
		vkDestroySemaphore(m_LogicalDevice, res.imageAquiredSemaphore, nullptr);
		vkDestroyCommandPool(m_LogicalDevice, res.commandPool, nullptr);
	}

	// Pipiline cleanup
	if(m_PipelineLayout)
	{
		vkDestroyPipelineLayout(m_LogicalDevice, m_PipelineLayout, nullptr);
	}
	if(m_Pipeline)
	{
		vkDestroyPipeline(m_LogicalDevice, m_Pipeline, nullptr);
	}

	// Cleanup shaders
	if(m_VertShader)
	{
		vkDestroyShaderModule(m_LogicalDevice, m_VertShader, nullptr);
	}
	if(m_FragShader)
	{
		vkDestroyShaderModule(m_LogicalDevice, m_FragShader, nullptr);
	}

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

void Engine::Render()
{
	if(m_RequireSwapchaingRecreation)
	{
		vkDeviceWaitIdle(m_LogicalDevice);
		DestroySwapchain();
		CreateSwapchain(m_Width, m_Height);
		m_RequireSwapchaingRecreation = false;
	}

	const uint32_t frameResIndex = m_FrameIndex++ % s_MaxFramesInFlight;
	const uint64_t signalValue = m_NextSignalValue++;
	const uint64_t waitValue = signalValue - s_MaxFramesInFlight;

	VkSemaphoreWaitInfo waitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &m_TimelineSemaphore,
		.pValues = &waitValue
	};
	vkWaitSemaphores(m_LogicalDevice, &waitInfo, UINT64_MAX);

	// Now it is safe to start recording commands
	FrameResources& res = m_FrameResources[frameResIndex];
	vkResetCommandPool(m_LogicalDevice, res.commandPool, 0);

	VkSemaphore imageAcquireSemaphore = res.imageAquiredSemaphore;

	uint32_t imageIndex = 0;
	VkResult acquireResult = vkAcquireNextImageKHR(m_LogicalDevice, m_Swapchain, UINT64_MAX, imageAcquireSemaphore, VK_NULL_HANDLE, &imageIndex);

	// Handle resize and out of date images, may need swapchain recreate
	if(acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_RequireSwapchaingRecreation = true;
		return;
	}
	else if(acquireResult == VK_SUBOPTIMAL_KHR)
	{
		// Can render this frame, recreate next time around
		m_RequireSwapchaingRecreation = true;
	}

	// Begin recording commands
	VkCommandBufferBeginInfo cmdBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	vkBeginCommandBuffer(res.commandBuffer, &cmdBeginInfo);

	// Transition the color and depth images
	std::vector<VkImageMemoryBarrier2> layoutBarriers
	{
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.image = m_SwapchainImages[imageIndex],
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		},
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.image = m_DepthImage,
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		}
	};

	VkDependencyInfo depInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = static_cast<uint32_t>(layoutBarriers.size()),
		.pImageMemoryBarriers = layoutBarriers.data()
	};
	vkCmdPipelineBarrier2(res.commandBuffer, &depInfo);

	VkRenderingAttachmentInfo colorAttachInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = m_SwapchainImageViews[imageIndex],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue{.color{0.01f,0.01f,0.01f,1}}
	};
	VkRenderingAttachmentInfo depthAttachInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = m_DepthImageView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.clearValue{.depthStencil{1.0f,0}}
	};

	VkRenderingInfo renderingInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea
		{
			.offset{.x = 0, .y = 0},
			.extent{.width = m_SwapchainWidth, .height = m_SwapchainHeight}
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachInfo,
		.pDepthAttachment = &depthAttachInfo
	};

	// Begin dynamic rendering
	vkCmdBeginRendering(res.commandBuffer, &renderingInfo);
	{
		VkViewport viewport
		{
			.x = 0, .y = 0,
			.width = static_cast<float>(m_SwapchainWidth),
			.height = static_cast<float>(m_SwapchainHeight)
		};
		vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);

		VkRect2D scissor
		{
			.offset{.x = 0, .y = 0},
			.extent{.width = m_SwapchainWidth, .height = m_SwapchainHeight}
		};
		vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

		// draw our triangle
		vkCmdBindPipeline(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
		vkCmdDraw(res.commandBuffer, 3, 1, 0, 0);
	}
	// End dynamic rendering
	vkCmdEndRendering(res.commandBuffer);

	// Transition the image from color attachment to presentation so we can show it
	VkImageMemoryBarrier2 presentLayoutBarrier
	{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_NONE,
			.dstAccessMask = 0,
			.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.image = m_SwapchainImages[imageIndex],
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
	};
	VkDependencyInfo presentDepInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &presentLayoutBarrier
	};
	vkCmdPipelineBarrier2(res.commandBuffer, &presentDepInfo);

	vkEndCommandBuffer(res.commandBuffer);

	// Ensure swapchain image is actually available to start color output
	VkSemaphoreSubmitInfo imageAcquireWaitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = imageAcquireSemaphore,
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
	};

	// Signal that image can be presented
	std::vector<VkSemaphoreSubmitInfo> semaphoreSignals
	{
		{ // Rebder work completion signal
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_RenderCompleteSemaphores[imageIndex],
			.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
		},
		{ // Entire frame is completed (timeline)
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_TimelineSemaphore,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		}
	};

	VkCommandBufferSubmitInfo cmdSubmitInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = res.commandBuffer
	};
	VkSubmitInfo2 submitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &imageAcquireWaitInfo,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdSubmitInfo,
		.signalSemaphoreInfoCount = static_cast<uint32_t>(semaphoreSignals.size()),
		.pSignalSemaphoreInfos = semaphoreSignals.data()
	};
	vkQueueSubmit2(m_GfxQueue, 1, &submitInfo, VK_NULL_HANDLE);

	// Present the image
	VkPresentInfoKHR presentInfo
	{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &m_RenderCompleteSemaphores[imageIndex],
		.swapchainCount = 1,
		.pSwapchains = &m_Swapchain,
		.pImageIndices = &imageIndex,
		.pResults = nullptr
	};

	vkQueuePresentKHR(m_GfxQueue, &presentInfo);
}

// ===========PRIVATE===========

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

	if(m_Pipeline = CreateGraphicsPipeline(); !m_Pipeline)
	{
		SDL_Log("Unable to initialize the graphics pipeline");
		return false;
	}

	if(!CreateSyncResources())
	{
		SDL_Log("Couldn't create the sync related resources");
		return false;
	}

	if(!CreateCommandBuffers())
	{
		SDL_Log("Coludn't create command buffer");
		return false;
	}

	return true;
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

	for(int currentFamIdx = 0; currentFamIdx < queueFamCount; ++currentFamIdx)
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
		vkDestroyImageView(m_LogicalDevice, swapchainImgView, nullptr);
	}
	m_SwapchainImageViews.clear();

	for(VkSemaphore& semaphore : m_RenderCompleteSemaphores)
	{
		vkDestroySemaphore(m_LogicalDevice, semaphore, nullptr);
	}
	m_RenderCompleteSemaphores.clear();

	if(m_Swapchain)
	{
		vkDestroySwapchainKHR(m_LogicalDevice, m_Swapchain, nullptr);
		m_Swapchain = nullptr;
	}

	if(m_DepthImageView)
	{
		vkDestroyImageView(m_LogicalDevice, m_DepthImageView, nullptr);
		vmaDestroyImage(m_VmaAllocator, m_DepthImage, m_DepthImageAllocation);
		m_DepthImageView = nullptr;
	}
}

VkShaderModule Engine::CreateShaderModule(const std::string& filename, shaderc_shader_kind kind) const
{
	// Read shader file from disk
	const std::string shaderPath = "src/shaders/" + filename;
	const std::string src = ReadTextFile(shaderPath);

	if(src.empty())
	{
		SDL_Log("Specified shader file doesn't exist: %s", shaderPath);
		return nullptr;
	}

	// Compile the shader to SPIR-V
	std::cout << "Compiling shader: " << shaderPath << std::endl;
	shaderc::Compiler compiler;
	shaderc::CompileOptions opts;
	opts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
	opts.SetTargetSpirv(shaderc_spirv_version_1_6);
	opts.SetOptimizationLevel(shaderc_optimization_level_performance);
	shaderc::CompilationResult result = compiler.CompileGlslToSpv(src, kind, filename.c_str(), opts);

	if (result.GetCompilationStatus() != shaderc_compilation_status_success)
	{
		std::cerr << "Shader Compilation Error: " << result.GetErrorMessage() << std::endl;
		return nullptr;
	}

	const size_t shaderSize = (result.cend() - result.cbegin()) * sizeof(uint32_t);

	// Pass Spir-V to vulkan and create shader-module
	VkShaderModuleCreateInfo moduleCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = shaderSize,
		.pCode = result.cbegin()
	};

	VkShaderModule shaderModule = nullptr;
	if(vkCreateShaderModule(m_LogicalDevice, &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		SDL_Log("Error creating shader module");
		return nullptr;
	}

	return shaderModule;
}

bool Engine::CreateShaders()
{
	if (m_VertShader = CreateShaderModule("shader.vert", shaderc_vertex_shader); !m_VertShader)
	{
		return false;
	}

	if (m_FragShader = CreateShaderModule("shader.frag", shaderc_fragment_shader); !m_FragShader)
	{
		return false;
	}

	return true;
}

VkPipeline Engine::CreateGraphicsPipeline()
{
	VkPipelineLayoutCreateInfo pipelineLayoutCraeteInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pushConstantRangeCount = 0
	};

	if(vkCreatePipelineLayout(m_LogicalDevice, &pipelineLayoutCraeteInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
	{
		SDL_Log("Unable to create the pipeline layout");
		return nullptr;
	}

	const char* entryPoint = "main";
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = m_VertShader,
			.pName = entryPoint
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = m_FragShader,
			.pName = entryPoint
		}

	};

	VkPipelineVertexInputStateCreateInfo vertInputInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
	};

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};

	VkPipelineDepthStencilStateCreateInfo depthStencilInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.stencilTestEnable = VK_FALSE
	};

	VkPipelineViewportStateCreateInfo viewportInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = nullptr,
		.scissorCount = 1,
		.pScissors = nullptr
	};

	VkPipelineRasterizationStateCreateInfo rasterInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f
	};

	VkPipelineMultisampleStateCreateInfo multiSampleInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};

	VkPipelineColorBlendAttachmentState attachState
	{
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};

	VkPipelineColorBlendStateCreateInfo blendInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachState
	};

	// Enable dynamic state
	std::vector<VkDynamicState> dynamicState
	{
		VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicStateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = static_cast<uint32_t>(dynamicState.size()),
		.pDynamicStates = dynamicState.data()
	};

	// Structure required for dynamic rendering
	VkPipelineRenderingCreateInfo renderInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &s_SwapchainFormat,
		.depthAttachmentFormat = s_DepthFormat
	};

	VkGraphicsPipelineCreateInfo pipelineInfo
	{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderInfo,
		.stageCount = static_cast<uint32_t>(shaderStages.size()),
		.pStages = shaderStages.data(),
		.pVertexInputState = &vertInputInfo,
		.pInputAssemblyState = &inputAssemblyInfo,
		.pViewportState = &viewportInfo,
		.pRasterizationState = &rasterInfo,
		.pMultisampleState = &multiSampleInfo,
		.pDepthStencilState = &depthStencilInfo,
		.pColorBlendState = &blendInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = m_PipelineLayout,
		.renderPass = VK_NULL_HANDLE
	};

	VkPipeline newPipeline;
	if(vkCreateGraphicsPipelines(m_LogicalDevice, nullptr, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
	{
		SDL_Log("Error creating pipeline");
		return nullptr;
	}

	return newPipeline;
}

bool Engine::CreateSyncResources()
{
	VkSemaphoreTypeCreateInfo semaphoreTypeInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = s_MaxFramesInFlight
	};

	VkSemaphoreCreateInfo semaphoreInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &semaphoreTypeInfo
	};

	if(vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_TimelineSemaphore) != VK_SUCCESS)
	{
		SDL_Log("Unable to create the timeline semaphore");
		return false;
	}

	// Per-frame image-acquire semaphores
	for(FrameResources& res : m_FrameResources)
	{
		// Create binary semaphores
		VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		if(vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &res.imageAquiredSemaphore) != VK_SUCCESS)
		{
			SDL_Log("Error creating the per-frame image-aquire semaphore");
			return false;
		}
	}

	return true;
}

bool Engine::CreateCommandBuffers()
{
	for(FrameResources& res : m_FrameResources)
	{
		VkCommandPoolCreateInfo poolInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = m_GfxQueueFamIdx
		};

		if(vkCreateCommandPool(m_LogicalDevice, &poolInfo, nullptr, &res.commandPool) != VK_SUCCESS)
		{
			SDL_Log("Unable to creatae command buffer pool");
			return false;
		}

		VkCommandBufferAllocateInfo cmdAllocInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = res.commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		};

		if(vkAllocateCommandBuffers(m_LogicalDevice, &cmdAllocInfo, &res.commandBuffer) != VK_SUCCESS)
		{
			SDL_Log("Unable to allocate command buffer");
			return false;
		}
	}

	return true;
}

std::string Engine::ReadTextFile(const std::string& filePath) const
{
	std::ifstream inFile(filePath);
	if(inFile.is_open())
	{
		std::stringstream buffer;
		buffer << inFile.rdbuf();
		const std::string output = buffer.str();
		inFile.close();
		return output;
	}
	return std::string();
}