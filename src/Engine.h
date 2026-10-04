#pragma once

#define VK_NO_PROTOTYPES

#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vector>

struct SDL_Window;
struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

class Engine
{
	constexpr static uint32_t s_VulkanVersion = VK_API_VERSION_1_4;
	constexpr static VkFormat s_SwapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;
	constexpr static VkFormat s_DepthFormat = VK_FORMAT_D32_SFLOAT;

public:
	Engine();
	bool Init(const char* title, int width, int height);
	void Run();
	void Shutdown();

private:
	bool InitializeVulkan();

	bool CreateVulkanInstance();
	bool CreateVulkanSurface();
	VkPhysicalDevice FindPhysicalDevice();
	bool FindGraphicsQueue();
	bool CreateLogicalDevice();
	bool InitializeVMA();
	bool CreateSwapchain(uint32_t width, uint32_t height);
	void DestroySwapchain();
	bool CreateShaders();

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData);

	bool m_Running;
	SDL_Window* m_Window;
	int m_Width;
	int m_Height;

	VkInstance m_VulkanInstance = nullptr;
	VkSurfaceKHR m_VulkanSurface = nullptr;
	VkPhysicalDevice m_PhysicalDevice = nullptr;
	VkDevice m_LogicalDevice = nullptr;

	uint32_t m_GfxQueueFamIdx = UINT32_MAX;
	VkQueue m_GfxQueue = nullptr;

	VkDevice m_Device = nullptr;
	VmaAllocator m_VmaAllocator = nullptr;

	// Swapchain related members
	VkSwapchainKHR m_Swapchain = nullptr;
	std::vector<VkImage> m_SwapchainImages;
	std::vector<VkImageView> m_SwapchainImageViews;
	std::vector<VkSemaphore> m_RenderCompleteSemaphores;
	bool requireSwapchaingRecreation = false;
	uint32_t m_SwapchainWidth = 0;
	uint32_t m_SwapchainHeight = 0;

	VkImage m_DepthImage = nullptr;
	VkImageView m_DepthImageView = nullptr;
	VmaAllocation m_DepthImageAllocation = nullptr;
};

