
/// @summary Define the data associated with a Vulkan runtime loader, used to access the Vulkan API.
struct OS_VULKAN_LOADER
{
    HMODULE                           LoaderHandle;                                  /// The module base address of the vulkan-1.dll loader module.

    size_t                            InstanceLayerCount;                            /// The number of VkLayerProperties instances in the InstanceLayerInfo array.
    VkLayerProperties                *InstanceLayerList;                             /// An array of VkLayerProperties structures describing supported instance-level layers.
    size_t                           *LayerExtensionCount;                           /// An array of InstanceLayerCount counts specifying the number of VkExtensionProperties per-layer.
    VkExtensionProperties           **LayerExtensionList;                            /// An array of InstanceLayerCount arrays describing supported per-layer extensions.
    size_t                            InstanceExtensionCount;                        /// The number of VkExtensionProperties structures in the InstanceExtensionInfo array.
    VkExtensionProperties            *InstanceExtensionList;                         /// An array of VkExtensionProperties structures describing supported instance-level extensions.

    OS_LAYER_VULKAN_ICD_FUNCTION     (vkGetInstanceProcAddr);                        /// The vkGetInstanceProcAddr function.
    OS_LAYER_VULKAN_GLOBAL_FUNCTION  (vkCreateInstance);                             /// The vkCreateInstance function.
    OS_LAYER_VULKAN_GLOBAL_FUNCTION  (vkEnumerateInstanceLayerProperties);           /// The vkEnumerateInstanceLayerProperties function.
    OS_LAYER_VULKAN_GLOBAL_FUNCTION  (vkEnumerateInstanceExtensionProperties);       /// The vkEnumerateInstanceExtensionProperties function.
};

/// @summary Define the data associated with a Vulkan physical device. Data is populated during instance creation.
struct OS_VULKAN_PHYSICAL_DEVICE
{
    VkPhysicalDevice                  DeviceHandle;                                  /// The Vulkan physical device object handle.
    VkPhysicalDeviceFeatures          Features;                                      /// Information about optional features supported by the physical device.
    VkPhysicalDeviceProperties        Properties;                                    /// Information about the type and vendor of the physical device.
    VkPhysicalDeviceMemoryProperties  HeapProperties;                                /// Information about the memory access features of the physical device.
    size_t                            QueueFamilyCount;                              /// The number of command queue families exposed by the device.
    VkQueueFamilyProperties          *QueueFamilyProperties;                         /// An array of QueueFamilyCount VkQueueFamiliyProperties describing command queue attributes.
    VkBool32                         *QueueFamilyCanPresent;                         /// An array of boolean values set to VK_TRUE if the corresponding queue family supports presentation commands.
    size_t                            LayerCount;                                    /// The number of device-level optional layers exposed by the device.
    VkLayerProperties                *LayerList;                                     /// An array of LayerCount VkLayerProperties descriptors for the device-level layers exposed by the device.
    size_t                           *LayerExtensionCount;                           /// An array of LayerCount count values specifying the number of extensions exposed by each device-level layer.
    VkExtensionProperties           **LayerExtensionList;                            /// An array of LayerCount VkExtensionProperties lists providing additional information on the layer-level extensions.
    size_t                            ExtensionCount;                                /// The number of device-level optional extensions exposed by the device.
    VkExtensionProperties            *ExtensionList;                                 /// An array of ExtensionCount VkExtensionProperties providing additional information about device-level extensions.
};

/// @summary Define the data associated with a Vulkan API instance, used to enumerate physical devices and capabilities.
struct OS_VULKAN_INSTANCE
{
    VkInstance                        InstanceHandle;                                /// The Vulkan context object returned by vkCreateInstance.
    size_t                            PhysicalDeviceCount;                           /// The number of physical devices available in the system.
    VkPhysicalDevice                 *PhysicalDeviceHandles;                         /// An array of PhysicalDeviceCount physical device handles.
    VkPhysicalDeviceType             *PhysicalDeviceTypes;                           /// An array of PhysicalDeviceCount physical device types.
    OS_VULKAN_PHYSICAL_DEVICE        *PhysicalDeviceList;                            /// An array of PhysicalDeviceCount physical device metadata.
    
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkCreateDevice);                               /// The vkCreateDevice function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkDestroyInstance);                            /// The vkDestroyInstance function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkEnumeratePhysicalDevices);                   /// The vkEnumeratePhysicalDevices function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceFeatures);                  /// The vkGetPhysicalDeviceFeatures function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties);                /// The vkGetPhysicalDeviceProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceMemoryProperties);          /// The vkGetPhysicalDeviceMemoryProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties);     /// The vkGetPhysicalDeviceQueueFamilyProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkEnumerateDeviceLayerProperties);             /// The vkEnumerateDeivceLayerProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkEnumerateDeviceExtensionProperties);         /// The vkEnumerateDeviceExtensionProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetDeviceProcAddr);                          /// The vkGetDeviceProcAddr function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkDestroySurfaceKHR);                          /// The VK_KHR_surface vkDestroySurfaceKHR function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceSupportKHR);         /// The VK_KHR_surface vkGetPhysicalDeviceSurfaceSupportKHR function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR);         /// The VK_KHR_surface vkGetPhysicalDeviceSurfaceFormatsKHR function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);    /// The VK_KHR_surface vkGetPhysicalDeviceSurfaceCapabilitiesKHR function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfacePresentModesKHR);    /// The VK_KHR_surface vkGetPhysicalDeviceSurfacePresentModesKHR function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkCreateWin32SurfaceKHR);                      /// The VK_KHR_win32_surface vkCreateWin32SurfaceKHR function.
};

/// @summary Define the data associated with a Vulkan logical device, used to manage resources and execute commands.
struct OS_VULKAN_DEVICE
{
    VkDevice                          LogicalDevice;                                 /// The handle of the logical device.
    VkPhysicalDevice                  PhysicalDevice;                                /// The handle of the associated physical device.
    VkPhysicalDeviceType              PhysicalDeviceType;                            /// The physical device type (integrated GPU, discrete GPU, etc.)
    OS_VULKAN_PHYSICAL_DEVICE        *PhysicalDeviceInfo;                            /// A pointer into the OS_VULKAN_INSTANCE::PhysicalDeviceList providing information on device capabilities.

    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyDevice);                              /// The vkDestroyDevice function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetDeviceQueue);                             /// The vkGetDeviceQueue function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDeviceWaitIdle);                             /// The vkDeviceWaitIdle function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateSwapchainKHR);                         /// The VK_KHR_swapchain vkCreateSwapchainKHR function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroySwapchainKHR);                        /// 
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetSwapchainImagesKHR);                      /// 
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkAcquireNextImageKHR);                        /// 
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkQueuePresentKHR);                            /// 
};

/// @summary Define the data associated with a presentation surface, used to present rendered graphics to the screen.
struct OS_PRESENTATION_SURFACE
{
    VkSurfaceKHR                      SurfaceHandle;                                 /// The VK_KHR_win32_surface handle returned by vkCreateWin32SurfaceKHR.
    VkPhysicalDevice                  PhysicalDevice;                                /// The handle of the associated physical device.
    VkPhysicalDeviceType              PhysicalDeviceType;                            /// The physical device type (integrated GPU, discrete GPU, etc.)
    OS_VULKAN_PHYSICAL_DEVICE        *PhysicalDeviceInfo;                            /// A pointer into the OS_VULKAN_INSTANCE::PhysicalDeviceList. The OS_VULKAN_PHYSICAL_DEVICE::QueueFamilyCanPresent indicates whether a queue family can present to this surface.
    VkSurfaceCapabilitiesKHR          Capabilities;                                  /// Information about the capabilities and restrictions of the device when presenting to this surface.
    size_t                            QueueFamilyCount;                              /// The number of queue families exposed by the physical device.
    VkQueueFamilyProperties          *QueueFamilyProperties;                         /// Information about each queue family exposed by the physical device.
    VkBool32                         *QueueFamilyCanPresent;                         /// An array of boolean values set to VK_TRUE if the corresponding queue family supports presentation to this surface.
    size_t                            FormatCount;                                   /// The number of swap chain formats supported for presentation to this surface.
    VkSurfaceFormatKHR               *SupportedFormats;                              /// The set of supported swap chain formats and color spaces when presenting to this surface.
    size_t                            PresentModeCount;                              /// The number of presentation modes supported by the device when presenting to this surface.
    VkPresentModeKHR                 *SupportedPresentModes;                         /// The set of supported presentation mode identifiers supported by the device when presenting to this surface.
    HINSTANCE                         AppInstance;                                   /// The application HINSTANCE associated with the surface.
    HWND                              TargetWindow;                                  /// The application window handle associated with the surface.
    VkSwapchainKHR                    SwapchainHandle;                               /// The VK_KHR_swapchain handle returned by vkCreateSwapchainKHR.
    size_t                            SwapchainImageCount;                           /// The number of images in the swapchain.
    VkImage                          *SwapchainImages;                               /// An array of SwapchainImageCount image handles comprising the images in the swap chain.
    VkSwapchainCreateInfoKHR          SwapchainCreateInfo;                           /// A copy of the swapchain creation information, which can be used to recreate the swapchsain.
};

/// @summary Retrieve the OS_VULKAN_PHYSICAL_DEVICE object given the physical device handle.
/// @param vkinstance A valid OS_VULKAN_INSTANCE to search.
/// @param handle The Vulkan API physical device handle to locate.
/// @param index On return, this location stores the zero-based index of the device within the OS_VULKAN_INSTANCE.
/// @return A pointer to the physical device object, or NULL if the device handle could not be found.
internal_function OS_VULKAN_PHYSICAL_DEVICE*
OsFindVulkanPhysicalDevice
(
    OS_VULKAN_INSTANCE *vkinstance, 
    VkPhysicalDevice        handle, 
    size_t              *index=NULL
)
{
    for (size_t i = 0, n = vkinstance->PhysicalDeviceCount; i < n; ++i)
    {
        if (vkinstance->PhysicalDeviceHandles[i] == handle)
        {
            if (index != NULL) *index = i;
            return &vkinstance->PhysicalDeviceList[i];
        }
    }
    return NULL;
}

/// @summary Enumerate the displays attached to the system that have an attached desktop.
/// @param arena The memory arena from which to allocate the display list.
/// @param display_count On return, the number of attached displays is stored in this location.
/// @return A list of information about the displays attached to the system.
public_function OS_DISPLAY*
OsEnumerateAttachedDisplays
(
    OS_MEMORY_ARENA *arena, 
    size_t  &display_count
)
{
    DEVMODE     dm; dm.dmSize = sizeof(DEVMODE);
    DISPLAY_DEVICE  dd; dd.cb = sizeof(DISPLAY_DEVICE);
    OS_DISPLAY *display_list  = NULL;
    size_t      num_displays  = 0;
    DWORD         req_fields  = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    RECT                  rc  = {};
    
    // count the number of displays attached to the system.
    for (DWORD ordinal = 0; EnumDisplayDevices(NULL, ordinal, &dd, 0); ++ordinal)
    {   // ignore pseudo-displays and displays not attached to a desktop.
        if ((dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0)
            continue;
        if ((dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
            continue;
        num_displays++;
    }
    if ((display_list = OsMemoryArenaAllocateArray<OS_DISPLAY>(arena, num_displays)) == NULL)
    {
        OsLayerError("ERROR: %S: Insufficient memory to allocate display list.\n", __FUNCTION__);
        display_count = num_displays;
        return NULL;
    }
    OsZeroMemory(display_list, num_displays * sizeof(OS_DISPLAY));
    display_count = 0;

    // retrieve display properties.
    for (DWORD ordinal = 0; EnumDisplayDevices(NULL, ordinal, &dd, 0) && display_count < num_displays; ++ordinal)
    {   // ignore pseudo-displays and displays not attached to a desktop.
        if ((dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0)
            continue;
        if ((dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
            continue;

        if (!EnumDisplaySettingsEx(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0))
        {   // try for the settings saved in the registry instead.
            if (!EnumDisplaySettingsEx(dd.DeviceName, ENUM_REGISTRY_SETTINGS, &dm, 0))
            {   // unable to retrieve the diplay settings. skip the display.
                continue;
            }
        }
        if ((dm.dmFields & req_fields) != req_fields)
        {   // the DEVMODE doesn't report the required fields.
            continue;
        }

        rc.left    = dm.dmPosition.x;
        rc.top     = dm.dmPosition.y;
        rc.right   = dm.dmPosition.x + dm.dmPelsWidth;
        rc.bottom  = dm.dmPosition.y + dm.dmPelsHeight;
        display_list[display_count].Ordinal       = ordinal;
        display_list[display_count].Monitor       = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
        display_list[display_count].DisplayX      = dm.dmPosition.x;
        display_list[display_count].DisplayY      = dm.dmPosition.y;
        display_list[display_count].DisplayWidth  = dm.dmPelsWidth;
        display_list[display_count].DisplayHeight = dm.dmPelsHeight;
        OsCopyMemory(&display_list[display_count].DisplayMode, &dm, sizeof(DEVMODE));
        OsCopyMemory(&display_list[display_count].DisplayInfo, &dd, sizeof(DISPLAY_DEVICE));
        display_count++;
    }
    return display_list;
}

/// @summary Determine if a display is the current primary display attached to the system.
/// @param display The display object to check.
/// @return true if the specified display is the primary display.
public_function bool
OsIsPrimaryDisplay
(
    OS_DISPLAY const *display
)
{
    return (display->DisplayInfo.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
}

/// @summary Search a display list to locate the OS_DISPLAY representing the primary display.
/// @param display_list The list of display information to search.
/// @param display_count The number of displays in the display list.
/// @return A pointer to the primary display, or NULL.
public_function OS_DISPLAY*
OsPrimaryDisplay
(
    OS_DISPLAY  *display_list, 
    size_t const display_count
)
{
    for (size_t i = 0; i < display_count; ++i)
    {
        if (display_list[i].DisplayInfo.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
            return &display_list[i];
    }
    return NULL;
}

/// @summary Query a display for its current orientation.
/// @param display The display object to query.
/// @return One of OS_DISPLAY_ORIENTATION_PORTRAIT or OS_DISPLAY_ORIENTATION_LANDSCAPE.
public_function int
OsDisplayOrientation
(
    OS_DISPLAY const *display
)
{
    if (display->DisplayWidth >= display->DisplayHeight)
        return OS_DISPLAY_ORIENTATION_LANDSCAPE;
    else
        return OS_DISPLAY_ORIENTATION_PORTRAIT;
}

/// @summary Return the current refresh rate of a given display.
/// @param display The display to query.
/// @return The display refresh rate, in Hz.
public_function float
OsDisplayRefreshRate
(
    OS_DISPLAY const *display
)
{
    if (display->DisplayMode.dmDisplayFrequency == 0 || 
        display->DisplayMode.dmDisplayFrequency == 1)
    {   // a value of 0 or 1 indicates the 'default' refresh rate.
        HDC dc = GetDC(NULL);
        int hz = GetDeviceCaps(dc, VREFRESH);
        ReleaseDC(NULL, dc);
        return (float)  hz;
    }
    else
    {   // return the display frequency specified in the DEVMODE structure.
        return (float) display->DisplayMode.dmDisplayFrequency;
    }
}

/// @summary Determine whether an instance-level layer is supported by the runtime.
/// @param vkloader A valid OS_VULKAN_LOADER to search.
/// @param layer_name A zero-terminated ASCII string specifying the registered name of the layer to locate.
/// @param layer_index If non-NULL, and the named layer is supported, this location is updated with the zero-based index of the layer in the list.
/// @return The layer name string (of the associated VkLayerProperties), if supported; otherwise, NULL.
public_function char const*
OsSupportsVulkanInstanceLayer
(
    OS_VULKAN_LOADER const   *vkloader,
    char             const *layer_name, 
    size_t            *layer_index=NULL
)
{
    return OsSupportsVulkanLayer(layer_name, vkloader->InstanceLayerList, vkloader->InstanceLayerCount, layer_index);
}

/// @summary Determine whether the runtime supports an entire set of instance-level layers.
/// @param vkloader A valid OS_VULKAN_LOADER to search.
/// @param layer_names An array of zero-terminated ASCII strings specifying the registered name of each layer to validate.
/// @param layer_count The number of strings in the layer_names array.
/// @return true if the runtime supports the entire set of instance-level layers.
public_function bool
OsSupportsAllVulkanInstanceLayers
(
    OS_VULKAN_LOADER const     *vkloader, 
    char             const **layer_names, 
    size_t           const   layer_count
)
{
    for (size_t i = 0; i < layer_count; ++i)
    {
        if (OsSupportsVulkanInstanceLayer(vkloader, layer_names[i]) == NULL)
        {
            return false;
        }
    }
    return true;
}

/// @summary Determine whether an instance-level extension is supported by the runtime.
/// @paramvk vkloader A valid OS_VULKAN_LOADER to search.
/// @param extension_name A zero-terminated ASCII string specifying the registered name of the extension to locate.
/// @param extension_index If non-NULL, and the named extension is supported, this location is updated with the zero-based index of the extension in the list.
/// @return The extension name string (of the associated VkExtensionProperties), if supported; otherwise, NULL.
public_function char const*
OsSupportsVulkanInstanceExtension
(
    OS_VULKAN_LOADER const       *vkloader, 
    char             const *extension_name, 
    size_t            *extension_index=NULL
)
{
    return OsSupportsVulkanExtension(extension_name, vkloader->InstanceExtensionList, vkloader->InstanceExtensionCount, extension_index);
}

/// @summary Determine whether the runtime supports an entire set of instance-level extensions.
/// @param vkloader A valid OS_VULKAN_LOADER to search.
/// @param extension_names An array of zero-terminated ASCII strings specifying the registered name of each extension to validate.
/// @param extension_count The number of strings in the extension_names array.
/// @return true if the runtime supports the entire set of instance-level extensions.
public_function bool
OsSupportsAllVulkanInstanceExtensions
(
    OS_VULKAN_LOADER const         *vkloader, 
    char             const **extension_names, 
    size_t           const   extension_count
)
{
    for (size_t i = 0; i < extension_count; ++i)
    {
        if (OsSupportsVulkanInstanceExtension(vkloader, extension_names[i]) == NULL)
        {
            return false;
        }
    }
    return true;
}

/// @summary Create a new Vulkan runtime loader object. This is the first step in accessing Vulkan functionality.
/// @param vkloader The Vulkan runtime loader to initialize.
/// @param arena The memory arena to use when allocating storage for Vulkan instance layer and extension information.
/// @param result If the function returns OS_VULKAN_LOADER_RESULT_VKERROR, the Vulkan result code is stored at this location.
/// @return One of OS_VULKAN_LOADER_RESULT indicating the result of the operation.
public_function int
OsCreateVulkanLoader
(
    OS_VULKAN_LOADER *vkloader,
    OS_MEMORY_ARENA     *arena, 
    VkResult           &result
)
{   
    HMODULE     dll_instance = NULL;
    uint32_t     layer_count = 0;
    uint32_t layer_ext_count = 0;
    uint32_t extension_count = 0;
    int             ldresult = OS_VULKAN_LOADER_RESULT_SUCCESS;
    os_arena_marker_t marker = OsMemoryArenaMark(arena);

    // initialize all of the fields of the runtime loader instance.
    ZeroMemory(vkloader, sizeof(OS_VULKAN_LOADER));
    result = VK_SUCCESS;

    // attempt to load the LunarG Vulkan loader into the process address space.
    if ((dll_instance = LoadLibrary(_T("vulkan-1.dll"))) == NULL)
    {   // if the LunarG loader isn't available, assume no ICD is present.
        return OS_VULKAN_LOADER_RESULT_NOVULKAN;
    }

    // initialize the base loader object fields. this is required prior to resolving additional entry points.
    vkloader->LoaderHandle = dll_instance;

    // resolve the core Vulkan loader entry points.
    if (!OsVulkanLoaderResolveIcdFunctions(vkloader))
    {   // the loader will be unable to load additional required entry points or create an instance.
        ldresult = OS_VULKAN_LOADER_RESULT_NOENTRY;
        goto cleanup_and_fail;
    }
    if (!OsVulkanLoaderResolveGlobalFunctions(vkloader))
    {   // the loader will be unable to create an instance or enumerate devices.
        ldresult = OS_VULKAN_LOADER_RESULT_NOENTRY;
        goto cleanup_and_fail;
    }

    // enumerate supported instance-level layers and extensions. get the count first, followed by the data.
    if ((result = vkloader->vkEnumerateInstanceLayerProperties(&layer_count, NULL)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the number of Vulkan instance layers exposed by the runtime (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    if ((result = vkloader->vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the number of Vulkan instance extensions exposed by the runtime (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    if (layer_count != 0)
    {   // allocate memory for and retrieve layer and layer extension information.
        vkloader->InstanceLayerCount  = layer_count;
        vkloader->InstanceLayerList   = OsMemoryArenaAllocateArray<VkLayerProperties     >(arena, layer_count);
        vkloader->LayerExtensionCount = OsMemoryArenaAllocateArray<size_t                >(arena, layer_count);
        vkloader->LayerExtensionList  = OsMemoryArenaAllocateArray<VkExtensionProperties*>(arena, layer_count);
        if (vkloader->InstanceLayerList == NULL || vkloader->LayerExtensionCount == NULL || vkloader->LayerExtensionList == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan instance layer and extension information.\n", __FUNCTION__, GetCurrentThreadId());
            ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
            goto cleanup_and_fail;
        }
        if ((result = vkloader->vkEnumerateInstanceLayerProperties(&layer_count, vkloader->InstanceLayerList)) < 0)
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve the set of Vulkan instance layers exposed by the runtime (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
            goto cleanup_and_fail;
        }
        for (size_t i = 0, n = vkloader->InstanceLayerCount; i < n; ++i)
        {
            if ((result = vkloader->vkEnumerateInstanceExtensionProperties(vkloader->InstanceLayerList[i].layerName, &layer_ext_count, NULL)) < 0)
            {
                OsLayerError("ERROR: %S(%u): Unable to retrieve the number of Vulkan instance layer extensions exposed by runtime layer %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vkloader->InstanceLayerList[i].layerName, result);
                ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
                goto cleanup_and_fail;
            }
            if (layer_ext_count != 0)
            {   // allocate memory for and retrieve the layer extension information.
                vkloader->LayerExtensionCount[i] = layer_ext_count;
                if ((vkloader->LayerExtensionList[i] = OsMemoryArenaAllocateArray<VkExtensionProperties>(arena, layer_ext_count)) == NULL)
                {
                    OsLayerError("ERROR: %S(%u): Unable to allocate memory for extensions exposed by Vulkan runtime layer %S.\n", __FUNCTION__, GetCurrentThreadId(), vkloader->InstanceLayerList[i].layerName);
                    ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
                    goto cleanup_and_fail;
                }
                if ((result = vkloader->vkEnumerateInstanceExtensionProperties(vkloader->InstanceLayerList[i].layerName, &layer_ext_count, vkloader->LayerExtensionList[i])) < 0)
                {
                    OsLayerError("ERROR: %S(%u): Unable to retrieve the set of extensions exposed by Vulkan runtime layer %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vkloader->InstanceLayerList[i].layerName, result);
                    ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
                    goto cleanup_and_fail;
                }
            }
            else
            {   // don't allocate memory for zero-size arrays.
                vkloader->LayerExtensionCount[i] = 0;
                vkloader->LayerExtensionList [i] = NULL;
            }
        }
    }
    else
    {   // don't allocate memory for zero-size arrays.
        vkloader->InstanceLayerCount  = 0;
        vkloader->InstanceLayerList   = NULL;
        vkloader->LayerExtensionCount = NULL;
        vkloader->LayerExtensionList  = NULL;
    }
    if (extension_count != 0)
    {   // allocate memory for and retrieve the extension information.
        vkloader->InstanceExtensionCount = extension_count;
        if ((vkloader->InstanceExtensionList = OsMemoryArenaAllocateArray<VkExtensionProperties>(arena, extension_count)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan instance extension list.\n", __FUNCTION__, GetCurrentThreadId());
            ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
            goto cleanup_and_fail;
        }
        if ((result = vkloader->vkEnumerateInstanceExtensionProperties(NULL, &extension_count, vkloader->InstanceExtensionList)) < 0)
        {
            OsLayerError("ERROR: %S(%u): Unable to enumerate Vulkan instance extension properties (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
            goto cleanup_and_fail;
        }
    }
    else
    {   // don't allocate memory for zero-size arrays.
        vkloader->InstanceExtensionCount = 0;
        vkloader->InstanceExtensionList  = NULL;
    }

    // everything is complete. the next step is to call OsCreateVulkanInstance().
    return OS_VULKAN_LOADER_RESULT_SUCCESS;

cleanup_and_fail:
    if (dll_instance != NULL) CloseHandle(dll_instance);
    ZeroMemory(vkloader, sizeof(OS_VULKAN_LOADER));
    OsMemoryArenaResetToMarker(arena, marker);
    return ldresult;
}

/// @summary Create a new Vulkan instance object and retrieve information about the physical devices in the host system.
/// @param vkinstance The OS_VULKAN_INSTANCE to initialize.
/// @param vkloader A valid OS_VULKAN_LOADER used to resolve API entry points.
/// @param arena The memory arena to use when allocating storage for Vulkan physical device information.
/// @param create_info The VkInstanceCreateInfo to pass to vkCreateInstance.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateInstance.
/// @param result If the function returns OS_VULKAN_LOADER_RESULT_VKERROR, the Vulkan result code is stored at this location.
/// @return One of OS_VULKAN_LOADER_RESULT indicating the result of the operation.
public_function int
OsCreateVulkanInstance
(
    OS_VULKAN_INSTANCE                    *vkinstance,
    OS_VULKAN_LOADER                        *vkloader, 
    OS_MEMORY_ARENA                            *arena,
    VkInstanceCreateInfo  const          *create_info, 
    VkAllocationCallbacks const *allocation_callbacks, 
    VkResult                                  &result
)
{
    os_arena_marker_t marker = OsMemoryArenaMark(arena);
    uint32_t    device_count = 0;
    int             ldresult = OS_VULKAN_LOADER_RESULT_SUCCESS;

    // initialize all of the fields of the Vulkan instance.
    ZeroMemory(vkinstance, sizeof(OS_VULKAN_INSTANCE));
    result = VK_SUCCESS;

    // create the Vulkan API context (instance) used to enumerate physical devices and extensions.
    if ((result = vkloader->vkCreateInstance(create_info, allocation_callbacks, &vkinstance->InstanceHandle)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to create Vulkan instance (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    // resolve the instance-level API functions required to enumerate physical devices.
    if (!OsVulkanLoaderResolveInstanceFunctions(vkloader, vkinstance, create_info))
    {
        ldresult = OS_VULKAN_LOADER_RESULT_NOENTRY;
        goto cleanup_and_fail;
    }
    // query the number of physical devices installed in the system. 
    // allocate memory for the various physical device attribute arrays.
    if ((result = vkinstance->vkEnumeratePhysicalDevices(vkinstance->InstanceHandle, &device_count, NULL)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the number of Vulkan physical devices in the system (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    vkinstance->PhysicalDeviceCount   = device_count;
    vkinstance->PhysicalDeviceHandles = OsMemoryArenaAllocateArray<VkPhysicalDevice         >(arena, device_count);
    vkinstance->PhysicalDeviceTypes   = OsMemoryArenaAllocateArray<VkPhysicalDeviceType     >(arena, device_count);
    vkinstance->PhysicalDeviceList    = OsMemoryArenaAllocateArray<OS_VULKAN_PHYSICAL_DEVICE>(arena, device_count);
    if (vkinstance->PhysicalDeviceHandles == NULL || vkinstance->PhysicalDeviceTypes == NULL || vkinstance->PhysicalDeviceList == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device properties.\n", __FUNCTION__, GetCurrentThreadId());
        ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
        goto cleanup_and_fail;
    }
    // retrieve the physical device handles, and then query the runtime for their attributes.
    if ((result = vkinstance->vkEnumeratePhysicalDevices(vkinstance->InstanceHandle, &device_count, vkinstance->PhysicalDeviceHandles)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to enumerate Vulkan physical devices (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    for (size_t i = 0, n = vkinstance->PhysicalDeviceCount; i < n; ++i)
    {
        OS_VULKAN_PHYSICAL_DEVICE *dev =&vkinstance->PhysicalDeviceList[i];
        VkPhysicalDevice   handle = vkinstance->PhysicalDeviceHandles[i];
        uint32_t     family_count = 0;
        uint32_t      layer_count = 0;
        uint32_t  layer_ext_count = 0;
        uint32_t  extension_count = 0;

        dev->DeviceHandle = handle;
        vkinstance->vkGetPhysicalDeviceFeatures(handle, &dev->Features);
        vkinstance->vkGetPhysicalDeviceProperties(handle, &dev->Properties);
        vkinstance->vkGetPhysicalDeviceMemoryProperties(handle, &dev->HeapProperties);
        vkinstance->vkGetPhysicalDeviceQueueFamilyProperties(handle, &family_count, NULL);
        if (family_count != 0)
        {   // allocate storage for and retrieve queue family information.
            if ((dev->QueueFamilyProperties = OsMemoryArenaAllocateArray<VkQueueFamilyProperties>(arena, family_count)) == NULL)
            {
                OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device queue family properties.\n", __FUNCTION__, GetCurrentThreadId());
                ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
                goto cleanup_and_fail;
            }
            vkinstance->vkGetPhysicalDeviceQueueFamilyProperties(handle, &family_count, dev->QueueFamilyProperties);
            dev->QueueFamilyCount = family_count;
        }
        else
        {   // don't allocate any memory for zero-size arrays.
            dev->QueueFamilyCount = 0;
            dev->QueueFamilyProperties = NULL;
        }
        if ((result = vkinstance->vkEnumerateDeviceLayerProperties(handle, &layer_count, NULL)) < 0)
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve the number of layers exposed by Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), dev->Properties.deviceName, result);
            ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
            goto cleanup_and_fail;
        }
        if ((result = vkinstance->vkEnumerateDeviceExtensionProperties(handle, NULL, &extension_count, NULL)) < 0)
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve the number of extensions exposed by Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), dev->Properties.deviceName, result);
            ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
            goto cleanup_and_fail;
        }
        if (layer_count != 0)
        {   // retrieve the layer and layer extension information.
            dev->LayerCount          = layer_count;
            dev->LayerList           = OsMemoryArenaAllocateArray<VkLayerProperties     >(arena, layer_count);
            dev->LayerExtensionCount = OsMemoryArenaAllocateArray<size_t                >(arena, layer_count);
            dev->LayerExtensionList  = OsMemoryArenaAllocateArray<VkExtensionProperties*>(arena, layer_count);
            if (dev->LayerList == NULL || dev->LayerExtensionCount == NULL || dev->LayerExtensionList == NULL)
            {
                OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device layer properties.\n", __FUNCTION__, GetCurrentThreadId());
                ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
                goto cleanup_and_fail;
            }
            if ((result = vkinstance->vkEnumerateDeviceLayerProperties(handle, &layer_count, dev->LayerList)) < 0)
            {
                OsLayerError("ERROR: %S(%u): Unable to retrieve layer information for Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), dev->Properties.deviceName, result);
                ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
                goto cleanup_and_fail;
            }
            for (size_t j = 0, m = dev->LayerCount; j < m; ++j)
            {   // retrieve the number of layer-level extensions for this layer.
                if ((result = vkinstance->vkEnumerateDeviceExtensionProperties(handle, dev->LayerList[j].layerName, &layer_ext_count, NULL)) < 0)
                {
                    OsLayerError("ERROR: %S(%u): Unable to retrieve the number of extensions exposed by layer %S on Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), dev->LayerList[j].layerName, dev->Properties.deviceName, result);
                    ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
                    goto cleanup_and_fail;
                }
                if (layer_ext_count != 0)
                {   // retrieve the layer-level device extension information.
                    dev->LayerExtensionCount[j] = layer_ext_count;
                    if ((dev->LayerExtensionList[j] = OsMemoryArenaAllocateArray<VkExtensionProperties>(arena, layer_ext_count)) == NULL)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device layer extension list.\n", __FUNCTION__, GetCurrentThreadId());
                        ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
                        goto cleanup_and_fail;
                    }
                    if ((result = vkinstance->vkEnumerateDeviceExtensionProperties(handle, dev->LayerList[j].layerName, &layer_ext_count, dev->LayerExtensionList[j])) < 0)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to retrieve extension information for layer %S on Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), dev->LayerList[j].layerName, dev->Properties.deviceName, result);
                        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
                        goto cleanup_and_fail;
                    }
                }
                else
                {   // don't allocate any memory for zero-size arrays.
                    dev->LayerExtensionCount[j] = 0;
                    dev->LayerExtensionList [j] = NULL;
                }
            }
        }
        else
        {   // don't allocate any memory for zero-size arrays.
            dev->LayerCount          = 0;
            dev->LayerList           = NULL;
            dev->LayerExtensionCount = NULL;
            dev->LayerExtensionList  = NULL;
        }
        if (extension_count != 0)
        {   // retrieve the device-level extension information.
            dev->ExtensionCount = extension_count;
            if ((dev->ExtensionList = OsMemoryArenaAllocateArray<VkExtensionProperties>(arena, extension_count)) == NULL)
            {
                OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device extension properties.\n", __FUNCTION__, GetCurrentThreadId());
                ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
                goto cleanup_and_fail;
            }
            if ((result = vkinstance->vkEnumerateDeviceExtensionProperties(handle, NULL, &extension_count, dev->ExtensionList)) < 0)
            {
                OsLayerError("ERROR: %S(%u): Unable to retrieve extension information for Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), dev->Properties.deviceName, result);
                ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
                goto cleanup_and_fail;
            }
        }
        else
        {   // don't allocate any memory for zero-size arrays.
            dev->ExtensionCount = 0;
            dev->ExtensionList  = NULL;
        }
        // finally, make sure to copy the device type up into the packed array.
        vkinstance->PhysicalDeviceTypes[i] = dev->Properties.deviceType;
    }

    return OS_VULKAN_LOADER_RESULT_SUCCESS;

cleanup_and_fail:
    if (vkinstance->InstanceHandle) vkinstance->vkDestroyInstance(vkinstance->InstanceHandle, allocation_callbacks);
    ZeroMemory(vkinstance, sizeof(OS_VULKAN_INSTANCE));
    OsMemoryArenaResetToMarker(arena, marker);
    return ldresult;
}

/// @summary Associate an application window with a Vulkan presentation surface.
/// @param vksurface The OS_PRESENTATION_SURFACE to initialize.
/// @param vkinstance A valid OS_VULKAN_INSTANCE that manages the physical_device.
/// @param arena The memory arena to use when allocating storage for Vulkan sorface information.
/// @param physical_device The handle of the physical device to associate with the presentation surface.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateWin32SurfaceKHR.
/// @param instance The HINSTANCE or image base address of the application that created the target window.
/// @param window The handle of the window representing the presentation surface.
/// @param result If the function returns OS_VULKAN_LOADER_RESULT_VKERROR, the Vulkan result code is stored at this location.
/// @return One of OS_VULKAN_LOADER_RESULT indicating the result of the operation.
public_function int
OsCreatePresentationSurface
(
    OS_PRESENTATION_SURFACE                *vksurface, 
    OS_VULKAN_INSTANCE                    *vkinstance, 
    OS_MEMORY_ARENA                            *arena,
    VkPhysicalDevice                  physical_device,
    VkAllocationCallbacks const *allocation_callbacks, 
    HINSTANCE                                instance, 
    HWND                                       window,
    VkResult                                  &result
)
{
    VkWin32SurfaceCreateInfoKHR create_info = {};
    OS_VULKAN_PHYSICAL_DEVICE    *vkphysdev = OsFindVulkanPhysicalDevice(vkinstance, physical_device);
    os_arena_marker_t                marker = OsMemoryArenaMark(arena);
    uint32_t                   family_count = 0;
    uint32_t                   format_count = 0;
    uint32_t                     mode_count = 0;
    int                            ldresult = OS_VULKAN_LOADER_RESULT_SUCCESS;

    // initialize all of the fields of the Vulkan surface (and swap chain.)
    OsZeroMemory(vksurface, sizeof(OS_PRESENTATION_SURFACE));

    // ensure that a valid physical device was specified.
    if (vkphysdev == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to locate Vulkan physical device %p.\n", __FUNCTION__, GetCurrentThreadId(), physical_device);
        ldresult = OS_VULKAN_LOADER_RESULT_NOVULKAN;
        return NULL;
    }

    // copy properties of the physical device up to the logical device object.
    vksurface->PhysicalDevice     = physical_device;
    vksurface->PhysicalDeviceType = vkphysdev->Properties.deviceType;
    vksurface->PhysicalDeviceInfo = vkphysdev;

    // create the OS-specific VkSurfaceKHR object.
    create_info.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.pNext     = NULL;
    create_info.flags     = 0;
    create_info.hinstance = instance;
    create_info.hwnd      = window;
    if ((result = vkinstance->vkCreateWin32SurfaceKHR(vkinstance->InstanceHandle, &create_info, allocation_callbacks, &vksurface->SurfaceHandle)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to create Win32 Vulkan surface (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    if ((result = vkinstance->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, vksurface->SurfaceHandle, &vksurface->Capabilities)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve Win32 Vulkan surface capabilities (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }

    // determine which queue families, if any, support presentation to the new surface.
    vkinstance->vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, NULL);
    vksurface->QueueFamilyProperties = OsMemoryArenaAllocateArray<VkQueueFamilyProperties>(arena, family_count);
    vksurface->QueueFamilyCanPresent = OsMemoryArenaAllocateArray<VkBool32>(arena, family_count);
    if (vksurface->QueueFamilyProperties == NULL || vksurface->QueueFamilyCanPresent == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device queue family properties.\n", __FUNCTION__, GetCurrentThreadId());
        ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
        goto cleanup_and_fail;
    }
    for (uint32_t i = 0, n = (uint32_t) vkphysdev->QueueFamilyCount; i < n; ++i)
    {
        if ((result = vkinstance->vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, vksurface->SurfaceHandle, &vksurface->QueueFamilyCanPresent[i])) != VK_SUCCESS)
        {
            OsLayerError("ERROR: %S(%u): Unable to determine surface presentation support for queue family %u of Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), i, vkphysdev->Properties.deviceName, result);
            ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
            goto cleanup_and_fail;
        }
    }
    vkinstance->vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, vksurface->QueueFamilyProperties);

    // retrieve the number of and properties of the surface formats supported by the device.
    if ((result = vkinstance->vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, vksurface->SurfaceHandle, &format_count, NULL)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to query the number of supported surface formats for Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vkphysdev->Properties.deviceName, result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    if ((vksurface->SupportedFormats = OsMemoryArenaAllocateArray<VkSurfaceFormatKHR>(arena, format_count)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate memory for %u surface formats for Vulkan physical device %S.\n", __FUNCTION__, GetCurrentThreadId(), format_count, vkphysdev->Properties.deviceName);
        ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
        goto cleanup_and_fail;
    }
    if ((result = vkinstance->vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, vksurface->SurfaceHandle, &format_count, vksurface->SupportedFormats)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the supported surface formats for Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vkphysdev->Properties.deviceName, result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }

    // retrieve the number and type of presentation modes supported by the device.
    if ((result = vkinstance->vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, vksurface->SurfaceHandle, &mode_count, NULL)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to query the number of supported presentation modes for Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vkphysdev->Properties.deviceName, result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    if ((vksurface->SupportedPresentModes = OsMemoryArenaAllocateArray<VkPresentModeKHR>(arena, mode_count)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate memory for %u presentation modes for Vulkan physical device %S.\n", __FUNCTION__, GetCurrentThreadId(), mode_count, vkphysdev->Properties.deviceName);
        ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
        goto cleanup_and_fail;
    }
    if ((result = vkinstance->vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, vksurface->SurfaceHandle, &mode_count, vksurface->SupportedPresentModes)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the supported presentation modes for Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vkphysdev->Properties.deviceName, result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }

    vksurface->QueueFamilyCount = family_count;
    vksurface->FormatCount      = format_count;
    vksurface->PresentModeCount = mode_count;
    vksurface->AppInstance      = instance;
    vksurface->TargetWindow     = window;
    return OS_VULKAN_LOADER_RESULT_SUCCESS;

cleanup_and_fail:
    if (vksurface->SurfaceHandle) vkinstance->vkDestroySurfaceKHR(vkinstance->InstanceHandle, vksurface->SurfaceHandle, allocation_callbacks);
    ZeroMemory(vksurface, sizeof(OS_PRESENTATION_SURFACE));
    OsMemoryArenaResetToMarker(arena, marker);
    return ldresult;
}

/// @summary Create a new Vulkan logical device which can be used for resource management and command submission.
/// @param vkdevice The OS_VULKAN_DEVICE instance to initialize.
/// @param vkinstance A valid OS_VULKAN_INSTANCE that manages the physical_device.
/// @param arena The memory arena to use when allocating storage for Vulkan logical device information.
/// @param physical_device The handle of the physical device on which logical device commands will execute.
/// @param create_info The VkDeviceCreateInfo to pass to vkCreateDevice.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateDevice.
/// @param result If the function returns OS_VULKAN_LOADER_RESULT_VKERROR, the Vulkan result code is stored at this location.
/// @return One of OS_VULKAN_LOADER_RESULT indicating the result of the operation.
public_function int
OsCreateVulkanDevice
(
    OS_VULKAN_DEVICE                        *vkdevice,
    OS_VULKAN_INSTANCE                    *vkinstance, 
    OS_MEMORY_ARENA                            *arena,
    VkPhysicalDevice                  physical_device,
    VkDeviceCreateInfo    const          *create_info, 
    VkAllocationCallbacks const *allocation_callbacks, 
    VkResult                                  &result
)
{
    OS_VULKAN_PHYSICAL_DEVICE *vkphysdev = OsFindVulkanPhysicalDevice(vkinstance, physical_device);
    os_arena_marker_t             marker = OsMemoryArenaMark(arena);
    int                         ldresult = OS_VULKAN_LOADER_RESULT_SUCCESS;

    // initialize all of the fields of the Vulkan device.
    ZeroMemory(vkdevice, sizeof(OS_VULKAN_DEVICE));

    // ensure that a valid physical device was specified.
    if (vkphysdev == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to locate Vulkan physical device %p.\n", __FUNCTION__, GetCurrentThreadId(), physical_device);
        ldresult = OS_VULKAN_LOADER_RESULT_NOVULKAN;
        return NULL;
    }

    // copy properties of the physical device up to the logical device object.
    vkdevice->PhysicalDevice     = physical_device;
    vkdevice->PhysicalDeviceType = vkphysdev->Properties.deviceType;
    vkdevice->PhysicalDeviceInfo = vkphysdev;

    // create the logical Vulkan device used to create resources and submit commands.
    if ((result = vkinstance->vkCreateDevice(physical_device, create_info, allocation_callbacks, &vkdevice->LogicalDevice)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to create logical device for Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vkphysdev->Properties.deviceName, result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    // resolve the device-level API functions required to interface with the device and resources.
    if (!OsVulkanLoaderResolveDeviceFunctions(vkinstance, vkdevice, create_info))
    {
        ldresult = OS_VULKAN_LOADER_RESULT_NOENTRY;
        goto cleanup_and_fail;
    }

    // TODO(rlk): I'm sure a bunch of enumeration code will go here.

    // ...
    return OS_VULKAN_LOADER_RESULT_SUCCESS;

cleanup_and_fail:
    ZeroMemory(vkdevice, sizeof(OS_VULKAN_DEVICE));
    OsMemoryArenaResetToMarker(arena, marker);
    return ldresult;
}

/// @summary Create a swapchain used to present rendered output to a surface.
/// @param vksurface The OS_PRESENTATION_SURFACE representing the surface for which the swapchain will be created.
/// @param vkdevice The OS_VULKAN_DEVICE representing the logical device used to presemt the swapchain to the surface.
/// @param arena The OS_MEMORY_ARENA used to allocate memory for swapchain image handles.
/// @param create_info The VkSwapchainCreateInfoKHR to pass to vkCreateSwapchainKHR.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateDevice.
/// @param result If the function returns OS_VULKAN_LOADER_RESULT_VKERROR, the Vulkan result code is stored at this location.
/// @return One of OS_VULKAN_LOADER_RESULT indicating the result of the operation.
public_function int
OsCreateSwapChainForSurface
(
    OS_PRESENTATION_SURFACE                   *vksurface, 
    OS_VULKAN_DEVICE                           *vkdevice,
    OS_MEMORY_ARENA                               *arena,
    VkSwapchainCreateInfoKHR const          *create_info, 
    VkAllocationCallbacks    const *allocation_callbacks,
    VkResult                                     &result
)
{
    os_arena_marker_t      marker = OsMemoryArenaMark(arena);
    uint32_t          image_count = 0;
    int                  ldresult = OS_VULKAN_LOADER_RESULT_SUCCESS;

    if ((result = vkdevice->vkCreateSwapchainKHR(vkdevice->LogicalDevice, create_info, allocation_callbacks, &vksurface->SwapchainHandle)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Failed to create swapchain for surface %p (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vksurface->SurfaceHandle, result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    if ((result = vkdevice->vkGetSwapchainImagesKHR(vkdevice->LogicalDevice, vksurface->SwapchainHandle, &image_count, NULL)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Failed to retrieve swapchain image count for surface %p (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vksurface->SurfaceHandle, result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    if ((vksurface->SwapchainImages = OsMemoryArenaAllocateArray<VkImage>(arena, image_count)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Failed to allocate memory for %u swapchain image handles for surface %p.\n", __FUNCTION__, GetCurrentThreadId(), image_count, vksurface->SurfaceHandle);
        ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
        goto cleanup_and_fail;
    }
    if ((result = vkdevice->vkGetSwapchainImagesKHR(vkdevice->LogicalDevice, vksurface->SwapchainHandle, &image_count, vksurface->SwapchainImages)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Failed to retrieve swapchain images for surface %p (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vksurface->SurfaceHandle, result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    OsCopyMemory(vksurface->SwapchainCreateInfo, create_info, sizeof(VkSwapchainCreateInfoKHR));
    vksurface->SwapchainCreateInfo.oldSwapchain = vksurface->SwapchainHandle;
    vksurface->SwapchainImageCount = image_count;
    return OS_VULKAN_LOADER_RESULT_SUCCESS;

cleanup_and_fail:
    if (vksurface->SwapchainHandle) vkdevice->vkDestroySwapchainKHR(vkdevice->LogicalDevice, vksurface->SwapchainHandle, allocation_callbacks);
    vksurface->SwapchainHandle      = VK_NULL_HANDLE;
    vksurface->SwapchainImageCount  = 0;
    vksurface->SwapchainImages      = NULL;
    OsMemoryArenaResetToMarker(arena, marker);
    return ldresult;
}


/// @summary Re-creates the swapchain for a surface using stored creation parameters. The previous swapchain is deleted.
/// @param vksurface The OS_PRESENTATION_SURFACE representing surface to which the swapchain is presented.
/// @param vkdevice The OS_VULKAN_DEVICE representing the logical device that created the swapchain.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateDevice.
/// @param result If the function returns OS_VULKAN_LOADER_RESULT_VKERROR, the Vulkan result code is stored at this location.
/// @return One of OS_VULKAN_LOADER_RESULT indicating the result of the operation.
public_function int
OsRecreateSwapChainForSurface
(
    OS_PRESENTATION_SURFACE                *vksurface, 
    OS_VULKAN_DEVICE                        *vkdevice, 
    VkAllocationCallbacks const *allocation_callbacks,
    VkResult                                  &result
)
{
    VkSwapchainKHR  old_swapchain = vksurface->SwapchainHandle;
    VkSwapchainKHR  new_swapchain = VK_NULL_HANDLE;
    uint32_t          image_count =(uint32_t) vksurface->SwapchainImageCount;
    int                  ldresult = OS_VULKAN_LOADER_RESULT_SUCCESS;

    if ((result = vkdevice->vkCreateSwapchainKHR(vkdevice->LogicalDevice, vksurface->SwapchainCreateInfo, allocation_callbacks, &new_swapchain)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Failed to recreate swapchain for surface %p (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vksurface->SurfaceHandle, result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    if ((result = vkdevice->vkGetSwapchainImagesKHR(vkdevice->LogicalDevice, new_swapchain, &image_count, vksurface->SwapchainImages)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Failed to retrieve swapchain images for surface %p (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), vksurface->SurfaceHandle, result);
        ldresult = OS_VULKAN_LOADER_RESULT_VKERROR;
        goto cleanup_and_fail;
    }
    if (old_swapchain != VK_NULL_HANDLE)
    {   // delete the previous swapchain.
        vkdevice->vkDestroySwapchainKHR(vkdevice->LogicalDevice, old_swapchain, allocation_callbacks);
    }
    vksurface->SwapchainCreateInfo.oldSwapchain = new_swapchain;
    vksurface->SwapchainHandle = new_swapchain;
    return OS_VULKAN_LOADER_RESULT_SUCCESS;

cleanup_and_fail:
    if (new_swapchain != VK_NULL_HANDLE) vkdevice->vkDestroySwapchainKHR(vkdevice->LogicalDevice, new_swapchain, allocation_callbacks);
    if (old_swapchain != VK_NULL_HANDLE) vkdevice->vkDestroySwapchainKHR(vkdevice->LogicalDevice, old_swapchain, allocation_callbacks);
    vksurface->SurfaceCreateInfo.oldSwapchain = VK_NULL_HANDLE;
    vksurface->SwapchainHandle = VK_NULL_HANDLE;
    for (size_t i = 0, n = vksurface->SwapchainImageCount; i < n; ++i)
    {
        vksurface->SwapchainImages[i] = VK_NULL_HANDLE;
    }
    return ldresult;
}

/// @summary Destroy the swapchain associated with a surface.
/// @param vksurface The OS_PRESENTATION_SURFACE representing the surface to which the swapchain is presented.
/// @param vkdevice The OS_VULKAN_DEVICE representing the logical device that created the swapchain.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateDevice.
public_function void
OsDestroySwapchain
(
    OS_PRESENTATION_SURFACE                *vksurface, 
    OS_VULKAN_DEVICE                        *vkdevice, 
    VkAllocationCallbacks const *allocation_callbacks
)
{
    if (vksurface->SwapchainHandle != VK_NULL_HANDLE)
    {
        vkdevice->vkDestroySwapchainKHR(vkdevice->LogicalDevice, vkdevice->SwapchainHandle, allocation_callbacks);
        vksurface->SwapchainHandle  = VK_NULL_HANDLE;
    }
    for (size_t i = 0, n = vksurface->SwapchainImageCount; i < n; ++i)
    {
        vksurface->SwapchainImages[i] = VK_NULL_HANDLE;
    }
    vksurface->SwapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
}

/// @summary Destroy the surface object connecting Vulkan to an application window. Any associated swapchain is also destroyed.
/// @param vksurface The OS_PRESENTATION_SURFACE representing the surface to destroy,
/// @param vkdevice The OS_VULKAN_DEVICE representing the logical device that created the swapchain.
/// @param vkinstance The OS_VULKAN_INSTANCE used to create the surface.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateDevice.
public_function void
OsDestroySurface
(
    OS_PRESENTATION_SURFACE                *vksurface, 
    OS_VULKAN_DEVICE                        *vkdevice, 
    OS_VULKAN_INSTANCE                    *vkinstance, 
    VkAllocationCallbacks const *allocation_callbacks
)
{
    if (vksurface->SwapchainHandle != VK_NULL_HANDLE)
    {
        vkdevice->vkDestroySwapchainKHR(vkdevice->LogicalDevice, vkdevice->SwapchainHandle, allocation_callbacks);
    }
    if (vksurface->SurfaceHandle != VK_NULL_HANDLE)
    {
        vkinstance->vkDestroySurfaceKHR(vkinstance->InstanceHandle, vksurface->SurfaceHandle, allocation_callbacks);
    }
    OsZeroMemory(vksurface, sizeof(OS_PRESENTATION_SURFACE));
}

// TODO(rlk): Async disk I/O system.
// TODO(rlk): Vulkan display driver.
// TODO(rlk): OpenGL display driver.
// TODO(rlk): Network driver.
// TODO(rlk): Low-latency audio driver.
