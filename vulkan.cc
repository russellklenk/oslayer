/*/////////////////////////////////////////////////////////////////////////////
/// @summary Test the Vulkan and display enumeration functionality.
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include "win32_oslayer.cc"

/*//////////////////
//   Data Types   //
//////////////////*/

/*///////////////
//   Globals   //
///////////////*/

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Implement the entry point of the application.
/// @param argc The number of arguments passed on the command line.
/// @param argv An array of @a argc zero-terminated strings specifying the command-line arguments.
/// @return Zero if the function completes successfully, or non-zero otherwise.
export_function int 
main
(
    int    argc, 
    char **argv
)
{
    OS_MEMORY_ARENA                     arena = {};
    OS_VULKAN_ICD_INFO            icd_list[4] = {};
    OS_VULKAN_RUNTIME_DISPATCH        runtime = {};
    OS_VULKAN_RUNTIME_PROPERTIES runtime_info = {};
    size_t                          icd_count = 0;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    // create the memory arena used to store the runtime properties and device list data.
    if (OsCreateMemoryArena(&arena, Megabytes(2), true, true) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize main memory arena.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }
    if (OsEnumerateVulkanDrivers(icd_list, 4, &icd_count) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to enumerate Vulkan ICDs on the host system.\n", __FUNCTION__, GetCurrentThreadId());
        return -2;
    }
    if (OsLoadVulkanRuntime(&runtime) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to locate a Vulkan API runtime.\n", __FUNCTION__, GetCurrentThreadId());
        return -3;
    }
    if (OsQueryVulkanRuntimeProperties(&runtime_info, &runtime, &arena) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve Vulkan runtime properties.\n", __FUNCTION__, GetCurrentThreadId());
        return -4;
    }

    VkApplicationInfo         app_info = {};
    VkInstanceCreateInfo instance_info = {};
    char const         *layer_names[1] = {
        "VK_LAYER_LUNARG_standard_validation"
    };
    char const  *instance_ext_names[3] = {
        "VK_KHR_surface", 
        "VK_KHR_win32_surface", 
        "VK_EXT_debug_report"
    };

    // make sure that all required layers and extensions are supported.
    if (!OsSupportsAllVulkanInstanceLayers(&runtime_info, layer_names, 1))
    {
        OsLayerError("ERROR: %S(%u): One or more required validation layers are not supported.\n", __FUNCTION__, GetCurrentThreadId());
        return -5;
    }
    if (!OsSupportsAllVulkanInstanceExtensions(&runtime_info, instance_ext_names, 3))
    {
        OsLayerError("ERROR: %S(%u): One or more required instance extensions are not supported.\n", __FUNCTION__, GetCurrentThreadId());
        return -6;
    }
    
    // create the Vulkan API instance and resolve all instance-level function pointers.
    app_info.sType                        = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pNext                        = NULL;
    app_info.pApplicationName             = "vulkan";
    app_info.applicationVersion           = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName                  = "null engine";
    app_info.engineVersion                = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion                   = VK_MAKE_VERSION(1, 0, 0);
    instance_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pNext                   = NULL;
    instance_info.flags                   = 0;
    instance_info.pApplicationInfo        =&app_info;
    instance_info.enabledLayerCount       = 1;
    instance_info.ppEnabledLayerNames     = layer_names;
    instance_info.enabledExtensionCount   = 3;
    instance_info.ppEnabledExtensionNames = instance_ext_names;
    
    OS_VULKAN_INSTANCE_DISPATCH     vkinstance = {};
    OS_VULKAN_PHYSICAL_DEVICE_LIST device_list = {};
    if (OsCreateVulkanInstance(&vkinstance, &runtime, &instance_info, NULL) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to create Vulkan API instance.\n", __FUNCTION__, GetCurrentThreadId());
        return -7;
    }
    if (OsEnumerateVulkanPhysicalDevices(&device_list, &vkinstance, &arena, (HINSTANCE) GetModuleHandle(NULL)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to enumerate physical devices and displays.\n", __FUNCTION__, GetCurrentThreadId());
        return -8;
    }

    // clean up everything else.
    OsFreeVulkanDriverList(icd_list, icd_count);
    OsDeleteMemoryArena(&arena);
    return 0;
}

