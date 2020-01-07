#include <cassert>
#include <iostream>
#include <unordered_map>
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 0
#include <vulkan/vulkan.hpp>

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	static const std::unordered_map<vk::DebugUtilsMessageSeverityFlagBitsEXT, std::string> levels = {
		{vk::DebugUtilsMessageSeverityFlagBitsEXT::eError, "[err]"},
		{vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning, "[warn]"},
		{vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo, "[info]"},
		{vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose, "[trace]"}
	};
	const auto lvl_name = levels.at((vk::DebugUtilsMessageSeverityFlagBitsEXT) messageSeverity);

	std::cerr << lvl_name << ": " << pCallbackData->pMessage << "\n";

	return VK_FALSE;
}

static auto debug_create_info = vk::DebugUtilsMessengerCreateInfoEXT({},
	vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
	vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
	vulkan_debug_callback);


vk::UniqueInstance create_vulkan_instance() {
	const auto application_info = vk::ApplicationInfo(
		"test", VK_MAKE_VERSION(0, 0, 1),
		"test", VK_MAKE_VERSION(0, 0, 1),
		VK_API_VERSION_1_1
	);

	static constexpr std::array<const char *, 1> layers = { "VK_LAYER_LUNARG_standard_validation" };
	static constexpr std::array<const char *, 1> extensions = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

	return vk::createInstanceUnique(vk::InstanceCreateInfo()
		.setPApplicationInfo(&application_info)
		.setEnabledLayerCount((uint32_t) layers.size())
		.setPpEnabledLayerNames(layers.data())
		.setEnabledExtensionCount((uint32_t) extensions.size())
		.setPpEnabledExtensionNames(extensions.data())
		.setPNext((vk::DebugUtilsMessengerCreateInfoEXT*) &debug_create_info)
	);
}

vk::UniqueDevice create_vulkan_device(const vk::PhysicalDevice & phy_device, uint32_t queue_family) {
	constexpr std::array<float, 1> queue_priorities = {1.0f};
	const std::array<vk::DeviceQueueCreateInfo, 1> queue_ci{
		vk::DeviceQueueCreateInfo(vk::DeviceQueueCreateFlags(), queue_family, (uint32_t) queue_priorities.size(), queue_priorities.data())
	};

	return phy_device.createDeviceUnique(vk::DeviceCreateInfo()
		.setQueueCreateInfoCount((uint32_t) queue_ci.size())
		.setPQueueCreateInfos(queue_ci.data())
		.setEnabledLayerCount(0)
		.setPpEnabledLayerNames(nullptr)
		.setEnabledExtensionCount(0)
		.setPpEnabledExtensionNames(nullptr)
		.setPEnabledFeatures(nullptr));
}


struct find_memory_type_result {
	uint32_t type_index;
	uint32_t heap_index;
};

inline std::vector<find_memory_type_result> find_memory_type(const vk::PhysicalDeviceMemoryProperties & mem_props, uint32_t typeFilter, vk::MemoryPropertyFlags flags) {
	std::vector<find_memory_type_result> r;

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
		if (typeFilter & (1 << i) && ((mem_props.memoryTypes[i].propertyFlags & flags) == flags)) {
			r.emplace_back(find_memory_type_result{ i, mem_props.memoryTypes[i].heapIndex });
		}
	}

	return r;
}


std::tuple<vk::UniqueImage, vk::UniqueDeviceMemory> create_device_backed_image(const vk::Device & device, const vk::PhysicalDeviceMemoryProperties & mem_props,
	const vk::Format format, int width, int height, const vk::ImageUsageFlags usage)
{
	auto image = device.createImageUnique(vk::ImageCreateInfo(vk::ImageCreateFlags(),
		vk::ImageType::e2D, format, vk::Extent3D(width, height, 1), 1, 1,
		vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
		usage, vk::SharingMode::eExclusive, 0, nullptr, vk::ImageLayout::eUndefined));

	const auto mem_req = device.getImageMemoryRequirements(image.get());

	const auto mem_types = find_memory_type(mem_props, mem_req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	if (mem_types.size() == 0)

		throw std::runtime_error("Failed to find suitable memory type");
	auto mem = device.allocateMemoryUnique(vk::MemoryAllocateInfo(mem_req.size, mem_types[0].type_index));
	device.bindImageMemory(image.get(), mem.get(), 0);

	return {std::move(image), std::move(mem)};
}


std::tuple<vk::UniqueBuffer, vk::UniqueDeviceMemory> create_host_backed_buffer(const vk::Device & device, const vk::PhysicalDeviceMemoryProperties & mem_props,
	size_t size, const vk::BufferUsageFlags usage)
{
	auto buffer = device.createBufferUnique(vk::BufferCreateInfo(vk::BufferCreateFlags(), size, usage, vk::SharingMode::eExclusive));

	const auto mem_req = device.getBufferMemoryRequirements(buffer.get());
	const auto mem_types = find_memory_type(mem_props, mem_req.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	if (mem_types.size() == 0)
		throw std::runtime_error("Failed to find suitable memory type for buffer");

	auto mem = device.allocateMemoryUnique(vk::MemoryAllocateInfo(mem_req.size, mem_types[0].type_index));
	device.bindBufferMemory(buffer.get(), mem.get(), 0);

	return {std::move(buffer), std::move(mem)};
}


void test_image_transfer(const vk::Device & device, const vk::CommandPool & command_pool, const vk::Queue & queue,
	const vk::PhysicalDeviceMemoryProperties & mem_props, vk::Format format, int width, int height)
{
	auto [image, image_mem] = create_device_backed_image(device, mem_props, format, width, height,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc);

	const size_t pix_size = 3 * 2;
	const size_t buffer_size = width * height * pix_size;
	const auto [buffer, buffer_mem] = create_host_backed_buffer(device, mem_props, buffer_size,
		vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst);

	const auto cmd = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(command_pool, vk::CommandBufferLevel::ePrimary, 1));
	const auto fence = device.createFenceUnique(vk::FenceCreateInfo());

	cmd[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	cmd[0].pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands,
		vk::DependencyFlagBits::eByRegion, nullptr, nullptr,
		vk::ImageMemoryBarrier(vk::AccessFlags(), vk::AccessFlags(), vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
		0, 0, image.get(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));

	const auto clear_color = vk::ClearColorValue(std::array<float, 4>{1.0f, 0, 0, 1.0f});
	cmd[0].clearColorImage(image.get(), vk::ImageLayout::eTransferDstOptimal, clear_color,
		vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

	const auto copy_region = vk::BufferImageCopy(0, width, height,
		vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
		vk::Offset3D(0, 0, 0), vk::Extent3D(width, height, 1));

	cmd[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
		vk::DependencyFlagBits::eByRegion, nullptr, nullptr,
		vk::ImageMemoryBarrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead,
		vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, 0, 0,
		image.get(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));

	cmd[0].copyImageToBuffer(image.get(), vk::ImageLayout::eTransferSrcOptimal, buffer.get(), copy_region);

	cmd[0].end();

	device.resetFences(fence.get());

	queue.submit(vk::SubmitInfo(0, nullptr, nullptr, 1, &cmd[0], 0, nullptr), fence.get());
	device.waitForFences(fence.get(), VK_TRUE, UINT64_MAX);

	const std::byte * mmap = reinterpret_cast<const std::byte *>(device.mapMemory(buffer_mem.get(), 0, VK_WHOLE_SIZE));
	const auto pixels = reinterpret_cast<const std::array<uint16_t, 3> *>(mmap);
	assert(pixels[0] == (std::array<uint16_t, 3>{0xFFFF, 0x0000, 0x0000}));
	assert(pixels[1] == (std::array<uint16_t, 3>{0xFFFF, 0x0000, 0x0000})); //fails here
	assert(pixels[2] == (std::array<uint16_t, 3>{0xFFFF, 0x0000, 0x0000}));
	device.unmapMemory(buffer_mem.get());
}


int main(int argc, char ** argv) {
	//index of physical device to use (as returned by vkEnumeratePhysicalDevices)
	constexpr int gpu_index = 0;
	try {
		const auto instance = create_vulkan_instance();

		const auto debug_messanger = instance->createDebugUtilsMessengerEXTUnique(debug_create_info, nullptr, vk::DispatchLoaderDynamic(instance.get(), ::vkGetInstanceProcAddr));

		const auto phy_devices = instance->enumeratePhysicalDevices();
		const auto phy_device = phy_devices[gpu_index];
		const auto props = phy_device.getProperties();
		const auto mem_props = phy_device.getMemoryProperties();
		std::cout << "Using device: " << props.deviceName << "\n";

		const auto queue_properties = phy_device.getQueueFamilyProperties();
		uint32_t queue_family = 0;

		const auto device = create_vulkan_device(phy_device, queue_family);

		const auto command_pool = device->createCommandPoolUnique(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlags(), queue_family));
		const auto queue = device->getQueue(queue_family, 0);

		const auto format = vk::Format::eR16G16B16Unorm;
		const auto width = 5462;
		const auto height = 2;
		//works with this size
		//const auto width = 256;
		//const auto height = 256;
		const auto format_props = phy_device.getFormatProperties(format);
		assert(width <= props.limits.maxImageDimension2D);
		assert(height <= props.limits.maxImageDimension2D);
		assert((format_props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eTransferSrc)
			&& (format_props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eTransferDst));

		test_image_transfer(device.get(), command_pool.get(), queue, mem_props, format, width, height);

	} catch (std::exception & e) {
		assert(false);
		std::cerr << "exception in main(): " << e.what() << "\n";
		return EXIT_FAILURE;
	} catch (...) {
		assert(false);
		std::cerr << "unknown exception in main()\n";
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}