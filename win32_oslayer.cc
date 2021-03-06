/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement the OS layer services for the Win32 platform, including
/// the following functionality:
/// - Memory arena (Memory resource management)
/// - Thread pool and task scheduler (CPU resource management)
/// - Disk I/O (Asynchronous storage resource management)
/// - HID I/O (User input device management)
/// - Low-latency audio (audio input and output device management)
/// - Vulkan WSI (Vulkan Window system interface and swap chain management)
///////////////////////////////////////////////////////////////////////////80*/
//
// TODO:
// Memory allocation works like so. We have OS_HOST_MEMORY_POOL from which the 
// user can allocate actual memory, but none of the interfaces work with the 
// memory pool or memory chunks. Instead, everything that needs memory accepts 
// it as a void *memory and size_t memory_size. Everything that needs memory 
// also provides a function like:
// bool OsQueryMemoryRequirement(arg1, arg2, size_t &requirement) or 
// bool OsQueryMemoryRequirement(OS_xxxx_INIT *init, size_t &requirement) to 
// calculate the worst-case amount of memory required. The user can call the 
// appropriate function to get the memory requirement, allocate the memory 
// however they want, and then call the appropriate OsCreate function. The 
// thing being created or initialized can then create for example an 
// OS_HOST_MEMORY_ARENA around the void *memory and size_t memory_size from 
// which it sub-allocates. This works in most cases but might require some 
// API redesign for Vulkan initialization, etc. where there are a varying 
// number of formats supported for a given device. In these cases, we might 
// want to have an API where the user fills out an array of formats they need 
// and queries the memory requirement for the result data. In some cases, it 
// would make more sense to have a fixed limit on the number of eg. devices, 
// and the user could set that limit in the INIT structure and pass it in.

/*////////////////////
//   Preprocessor   //
////////////////////*/
/// @summary Define static/dynamic library import/export for the compiler.
#ifndef library_function
    #if   defined(BUILD_DYNAMIC)
        #define library_function                     __declspec(dllexport)
    #elif defined(BUILD_STATIC)
        #define library_function
    #else
        #define library_function                     __declspec(dllimport)
    #endif
#endif /* !defined(library_function) */

/// @summary Tag used to mark a function as available for use outside of the current translation unit (the default visibility).
#ifndef export_function
    #define export_function                          library_function
#endif

/// @summary Tag used to mark a function as available for public use, but not exported outside of the translation unit.
#ifndef public_function
    #define public_function                          static
#endif

/// @summary Tag used to mark a function internal to the translation unit.
#ifndef internal_function
    #define internal_function                        static
#endif

/// @summary Tag used to mark a variable as local to a function, and persistent across invocations of that function.
#ifndef local_persist
    #define local_persist                            static
#endif

/// @summary Tag used to mark a variable as global to the translation unit.
#ifndef global_variable
    #define global_variable                          static
#endif

/// @summary Define some useful macros for specifying common resource sizes.
#ifndef Kilobytes
    #define Kilobytes(x)                            (size_t((x)) * size_t(1024))
#endif
#ifndef Megabytes
    #define Megabytes(x)                            (size_t((x)) * size_t(1024) * size_t(1024))
#endif
#ifndef Gigabytes
    #define Gigabytes(x)                            (size_t((x)) * size_t(1024) * size_t(1024) * size_t(1024))
#endif

/// @summary Define macros for controlling compiler inlining.
#ifndef never_inline
    #define never_inline                            __declspec(noinline)
#endif
#ifndef force_inline
    #define force_inline                            __forceinline
#endif

/// @summary Define the size of a single cacheline on the target architecture.
#ifndef OS_CACHELINE_SIZE
    #define OS_CACHELINE_SIZE                       64
#endif

/// @summary Define a macro to align a type or field to a cacheline boundary.
#ifndef OS_CACHELINE_ALIGN
    #define OS_CACHELINE_ALIGN                      __declspec(align(OS_CACHELINE_SIZE))
#endif

/// @summary Define the value indicating an unused device handle.
#ifndef OS_INPUT_DEVICE_HANDLE_NONE
    #define OS_INPUT_DEVICE_HANDLE_NONE             INVALID_HANDLE_VALUE
#endif

/// @summary Define the maximum number of input devices of each type.
#ifndef OS_MAX_INPUT_DEVICES
    #define OS_MAX_INPUT_DEVICES                    4
#endif

/// @summary Define a bitvector used to poll all possible gamepad ports (all bits set.)
#ifndef OS_ALL_GAMEPAD_PORTS
    #define OS_ALL_GAMEPAD_PORTS                    0xFFFFFFFFUL
#endif

/// @summary Define the value indicating that an input packet was dropped because too many devices of the specified type are attached.
#ifndef OS_INPUT_DEVICE_TOO_MANY
    #define OS_INPUT_DEVICE_TOO_MANY                ~size_t(0)
#endif

/// @summary Define the value indicating that a device was not found in the specified device list.
#ifndef OS_INPUT_DEVICE_NOT_FOUND
    #define OS_INPUT_DEVICE_NOT_FOUND               ~size_t(0)
#endif

/// @summary Define several constant values used internally by the task scheduler.
#ifndef OS_TASK_SCHEDULER_CONSTANTS
    #define OS_TASK_SCHEDULER_CONSTANTS
    #define OS_INVALID_TASK_ID                      0x7FFFFFFFL
    #define OS_MIN_TASK_POOLS                       1
    #define OS_MAX_TASK_POOLS                       4096
    #define OS_MIN_TASKS_PER_POOL                   2
    #define OS_MAX_TASKS_PER_POOL                   65536
    #define OS_TASK_ID_MASK_INDEX                   0x0000FFFFUL
    #define OS_TASK_ID_MASK_POOL                    0x0FFF0000UL
    #define OS_TASK_ID_MASK_TYPE                    0x10000000UL
    #define OS_TASK_ID_MASK_VALID                   0x80000000UL
    #define OS_TASK_ID_SHIFT_INDEX                  0
    #define OS_TASK_ID_SHIFT_POOL                   16
    #define OS_TASK_ID_SHIFT_TYPE                   28
    #define OS_TASK_ID_SHIFT_VALID                  31
#endif

/// @summary Helper macro to write a message to stdout.
#ifndef OsLayerOutput
    #ifndef OS_LAYER_NO_OUTPUT
        #define OsLayerOutput(fmt_str, ...)         _ftprintf(stdout, _T(fmt_str), __VA_ARGS__)
    #else
        #define OsLayerOutput(fmt_str, ...)         
    #endif
#endif

/// @summary Helper macro to write a message to stderr.
#ifndef OsLayerError
    #ifndef OS_LAYER_NO_OUTPUT
        #define OsLayerError(fmt_str, ...)          _ftprintf(stderr, _T(fmt_str), __VA_ARGS__)
    #else
        #define OsLayerError(fmt_str, ...)          
    #endif
#endif

/// @summary Helper macros to emit task profiler events and span markers.
/// @param env The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param span The OS_TASK_PROFILER_SPAN associated with the interval being measured.
/// @param fmt The printf-style format string.
/// @param ... Substitution arguments for the format string.
#ifdef  OS_DISABLE_TASK_PROFILER
    #define OsTaskEvent(env, fmt, ...)              
    #define OsTaskSpanEnter(env, span, fmt, ...)    
    #define OsTaskSpanLeave(env, span)              
#else
    #define OsTaskEvent(env, fmt, ...)              CvWriteAlertW((env)->TaskProfiler->MarkerSeries, _T(fmt), __VA_ARGS__)
    #define OsTaskSpanEnter(env, span, fmt, ...)    CvEnterSpanW((env)->TaskProfiler->MarkerSeries, &(span).CvSpan, _T(fmt), __VA_ARGS__)
    #define OsTaskSpanLeave(env, span)              CvLeaveSpan((span).CvSpan)
#endif

/// @summary Helper macro used to declare a scoped variable to automatically enter a span when a task begins execution and leave the span when it finished.
/// @param id The os_task_id_t of the task being executed.
/// @param env The OS_TASK_ENVIRONMENT associated with the thread executing the task.
#ifndef OS_PROFILE_TASK
    #ifdef  OS_DISABLE_TASK_PROFILER
        #define OS_PROFILE_TASK(id, env)    
    #else
        #define OS_PROFILE_TASK(id, env)            OS_TASK_SCOPE __cv_task_scope__(__FUNCTION__, (id), (env))
    #endif
#endif

/// @summary Macro used to declare a function resolved at runtime.
#ifndef OS_LAYER_DECLARE_RUNTIME_FUNCTION
#define OS_LAYER_DECLARE_RUNTIME_FUNCTION(retval, callconv, name, ...) \
    typedef retval (callconv *name##_Fn)(__VA_ARGS__);              \
    extern name##_Fn name##_Func
#endif

/// @summary Macro used to define a function resolved at runtime.
#ifndef OS_LAYER_DEFINE_RUNTIME_FUNCTION
#define OS_LAYER_DEFINE_RUNTIME_FUNCTION(name) \
    /* global_variable */ name##_Fn name##_Func = NULL
#endif

/// @summary Resolve a function pointer from a DLL at runtime. If the function is not available, set the function pointer to a stub function.
/// Some naming conventions must be followed for these macros to work. Given function name:
/// name = SomeFunction
/// The function pointer typedef should be        : SomeFunction_Fn
/// The global function pointer instance should be: SomeFunction_Func
/// The stub function should be                   : SomeFunction_Stub
/// The resolve call should be                    : RESOLVE_WIN32_RUNTIME_FUNCTION(dll_instance, SomeFunction)
#ifndef OS_LAYER_RESOLVE_RUNTIME_FUNCTION
    #define OS_LAYER_RESOLVE_RUNTIME_FUNCTION(dll, fname)                      \
        do {                                                                   \
            fname##_Func = (fname##_Fn) GetProcAddress(dll, #fname);           \
            if (fname##_Func == NULL) { fname##_Func = fname##_Stub; }         \
        __pragma(warning(push));                                               \
        __pragma(warning(disable:4127));                                       \
        } while (0);                                                           \
        __pragma(warning(pop))
#endif

/// @summary Determine whether a Win32 runtime function was resolved to its stub implementation.
#ifndef OS_LAYER_RUNTIME_STUB
    #define OS_LAYER_RUNTIME_STUB(name) \
        name##_Func == name##_Stub
#endif

/// @summary Used to declare a pointer to a function exported by the Vulkan ICD.
/// These functions are directly loadable using OS-provided dynamic loading mechanisms like GetProcAddress.
#ifndef OS_LAYER_VULKAN_ICD_FUNCTION
#define OS_LAYER_VULKAN_ICD_FUNCTION(fname) \
    PFN_##fname fname
#endif

/// @summary Helper macro used by OsVulkanLoaderResolveIcdFunctions to resolve a Vulkan ICD entry point using GetProcAddress.
/// @param vkruntime The OS_VULKAN_RUNTIME_DISPATCH maintaining the function pointer to the entry point.
/// @param fname The entry point to resolve.
#ifndef OS_LAYER_RESOLVE_VULKAN_ICD_FUNCTION
#define OS_LAYER_RESOLVE_VULKAN_ICD_FUNCTION(vkruntime, fname) \
        do {                                                                   \
            if (((vkruntime)->fname = (PFN_##fname) GetProcAddress((vkruntime)->LoaderHandle, #fname)) == NULL) { \
                OsLayerError("ERROR: %S(%u): Unable to resolve Vulkan ICD entry point \"%S\".\n" , __FUNCTION__, GetCurrentThreadId(), #fname); \
                return VK_ERROR_INCOMPATIBLE_DRIVER;                           \
            }                                                                  \
        __pragma(warning(push));                                               \
        __pragma(warning(disable:4127));                                       \
        } while (0);                                                           \
        __pragma(warning(pop))
#endif

/// @summary Used to declare the pointer to a global-level function, used to create a VkInstance or enumerate extensions and layers.
/// Global-level functions must be resolved using vkGetInstanceProcAddr, but specify NULL for the VkInstance argument.
#ifndef OS_LAYER_VULKAN_GLOBAL_FUNCTION
#define OS_LAYER_VULKAN_GLOBAL_FUNCTION(fname) \
    PFN_##fname fname
#endif

/// @summary Helper macro used by OsVulkanLoaderResolveGlobalFunctions to resolve a global-level function.
/// @param vkruntime The OS_VULKAN_RUNTIME_DISPATCH maintaining the function pointer to the entry point.
/// @param fname The entry point to resolve.
#ifndef OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION
    #define OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(vkruntime, fname) \
        do {                                                                   \
            if (((vkruntime)->fname = (PFN_##fname) (vkruntime)->vkGetInstanceProcAddr(NULL, #fname)) == NULL) { \
                OsLayerError("ERROR: %S(%u): Unable to resolve Vulkan global entry point \"%S\".\n", __FUNCTION__, GetCurrentThreadId(), #fname); \
                return VK_ERROR_INCOMPATIBLE_DRIVER;                           \
            }                                                                  \
        __pragma(warning(push));                                               \
        __pragma(warning(disable:4127));                                       \
        } while (0);                                                           \
        __pragma(warning(pop))
#endif

/// @summary Used to declare the pointer to an instance-level function, used to query hardware features.
/// Instance-level functions must be resolved using vkGetInstanceProcAddr and require a valid VkInstance.
#ifndef OS_LAYER_VULKAN_INSTANCE_FUNCTION
#define OS_LAYER_VULKAN_INSTANCE_FUNCTION(fname) \
    PFN_##fname fname
#endif

/// @summary Helper macro used by OsVulkanLoaderResolveInstanceFunctions to resolve an instance-level function.
/// @param vkinstance The OS_VULKAN_INSTANCE_DISPATCH maintaining the function pointer to the entry point.
/// @param vkruntime The OS_VULKAN_RUNTIME_DISPATCH maintaining the function pointer to the vkGetInstanceProcAddr entry point.
/// @param fname The entry point to resolve.
#ifndef OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION
    #define OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkruntime, fname) \
        do {                                                                   \
            if (((vkinstance)->fname = (PFN_##fname)(vkruntime)->vkGetInstanceProcAddr((vkinstance)->InstanceHandle, #fname)) == NULL) { \
                OsLayerError("ERROR: %S(%u): Unable to resolve Vuklan instance entry point \"%S\".\n", __FUNCTION__, GetCurrentThreadId(), #fname); \
                return VK_ERROR_INCOMPATIBLE_DRIVER;                           \
            }                                                                  \
        __pragma(warning(push));                                               \
        __pragma(warning(disable:4127));                                       \
        } while (0);                                                           \
        __pragma(warning(pop))
#endif

/// @summary Used to declare the pointer to a device-level function, used to perform operations and submit commands on a logical device.
/// Device-level functions must be resolved using vkGetDeviceProcAddr.
#ifndef OS_LAYER_VULKAN_DEVICE_FUNCTION
#define OS_LAYER_VULKAN_DEVICE_FUNCTION(fname) \
    PFN_##fname fname
#endif

/// @summary Helper macro used by VkLoaderResolveDeviceFunctions to resolve a device-level function.
/// @param vkdevice The OS_VULKAN_DEVICE_DISPATCH maintaining the function pointer to set.
/// @param vkinstance The OS_VULKAN_INSTANCE_DISPATCH maintaining the function pointer to the vkGetDeviceProcAddr entry point.
/// @param fname The entry point to resolve.
#ifndef OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION
    #define OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(vkdevice, vkinstance, fname) \
        do {                                                                   \
            if (((vkdevice)->fname = (PFN_##fname) (vkinstance)->vkGetDeviceProcAddr((vkdevice)->DeviceHandle, #fname)) == NULL) { \
                OsLayerError("ERROR: %S(%u): Unable to resolve Vulkan device entry point \"%S\".\n", __FUNCTION__, GetCurrentThreadId(), #fname); \
                return VK_ERROR_INCOMPATIBLE_DRIVER;                           \
            }                                                                  \
        __pragma(warning(push));                                               \
        __pragma(warning(disable:4127));                                       \
        } while (0);                                                           \
        __pragma(warning(pop))
#endif

/// @summary Copy a Vulkan API function pointer from one dispatch object to another.
/// @param dst The destination dispatch object, for example an OS_VULKAN_DEVICE_DISPATCH instance.
/// @param src The source dispatch object, for example an OS_VULKAN_INSTANCE_DISPATCH instance.
#ifndef OS_LAYER_COPY_VULKAN_FUNCTION
    #define OS_LAYER_COPY_VULKAN_FUNCTION(dst, src, fname) \
        (dst)->fname = (src)->fname
#endif

/*////////////////
//   Includes   //
////////////////*/
#ifndef OS_LAYER_NO_INCLUDES
    #include <type_traits>
    #include <atomic>
    #include <thread>

    #include <stddef.h>
    #include <stdint.h> 
    #include <stdarg.h>
    #include <assert.h>
    #include <inttypes.h>

    #include <process.h>
    #include <conio.h>
    #include <fcntl.h>
    #include <io.h>

    #include <tchar.h>
    #include <Windows.h>
    #include <Shlobj.h>
    #include <Shellapi.h>
    #include <strsafe.h>
    #include <XInput.h>
    #include <intrin.h>

    #include <mmdeviceapi.h>
    #include <audioclient.h>
    #include <functiondiscoverykeys_devpkey.h>

    #include <vulkan/vulkan.h>

    #include "cvmarkers.h"
#endif

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Forward-declare several public types.
struct OS_CPU_INFO;

struct OS_HOST_MEMORY_POOL;
struct OS_HOST_MEMORY_POOL_INIT;
struct OS_HOST_MEMORY_ALLOCATION;

struct OS_MEMORY_RANGE;
struct OS_ARENA_ALLOCATOR;
struct OS_BUDDY_BLOCK_INFO;
struct OS_BUDDY_ALLOCATOR;
struct OS_HOST_MEMORY_ARENA;
struct OS_HOST_MEMORY_ALLOCATOR;

struct OS_WORKER_THREAD;
struct OS_THREAD_POOL;
struct OS_THREAD_POOL_INIT;

struct OS_FILE_DATA;
struct OS_FILE_MAPPING;
struct OS_PATH_PARTS;

struct OS_IO_OPERATION;
struct OS_IO_OPERATION_RESULT;
struct OS_IO_REQUEST;
struct OS_IO_RESULT;
struct OS_IO_REQUEST_POOL;
struct OS_IO_THREAD_INIT;
struct OS_IO_THREAD_POOL;
struct OS_IO_THREAD_POOL_INIT;

struct OS_INPUT_SYSTEM;
struct OS_INPUT_EVENTS;
struct OS_POINTER_EVENTS;
struct OS_GAMEPAD_EVENTS;
struct OS_KEYBOARD_EVENTS;

struct OS_TASK_POOL;
struct OS_TASK_POOL_INIT;
struct OS_TASK_ENVIRONMENT;
struct OS_TASK_SCHEDULER;
struct OS_TASK_SCHEDULER_INIT;
struct OS_TASK_PROFILER;
struct OS_TASK_PROFILER_SPAN;
struct OS_TASK_FENCE;
struct OS_TASK_SCOPE;

struct OS_VULKAN_ICD_INFO;
struct OS_VULKAN_DEVICE_DISPATCH;
struct OS_VULKAN_RUNTIME_DISPATCH;
struct OS_VULKAN_INSTANCE_DISPATCH;
struct OS_VULKAN_RUNTIME_PROPERTIES;
struct OS_VULKAN_PHYSICAL_DEVICE_LIST;

struct OS_AUDIO_SYSTEM;
struct OS_AUDIO_DEVICE_LIST;
struct OS_AUDIO_OUTPUT_DEVICE;
struct OS_AUDIO_CAPTURE_DEVICE;

/// @summary Represents a pool of pre-allocated OS_HOST_MEMORY_ALLOCATION instances.
/// Typically each thread maintains its own OS_HOST_MEMORY_POOL from which it alone acquires and releases allocations.
struct OS_HOST_MEMORY_POOL
{
    char const                *Name;                 /// A nul-terminated string specifying the name of the pool. This value is used for debugging purposes only.
    OS_HOST_MEMORY_ALLOCATION *FreeList;             /// The first free OS_HOST_MEMORY_ALLOCATION instance.
    OS_HOST_MEMORY_ALLOCATION *NodeList;             /// The pre-allocated storage for Capacity OS_HOST_MEMORY_ALLOCATION instances.
    size_t                     Capacity;             /// The maximum number of allocations that can be made from the pool.
    size_t                     MinAllocationSize;    /// The minimum number of bytes that can be associated with any individual allocation.
    size_t                     MinCommitIncrease;    /// The minimum number of bytes that memory commitment can increase by for each allocation from the pool.
    uint32_t                   PageSize;             /// The size of a VMM page, in bytes, on the host operating system.
    uint32_t                   Granularity;          /// The VMM allocation granularity, in bytes.
};

/// @summary Define the data used to initialize a pool of OS_HOST_MEMORY_ALLOCATION instances.
struct OS_HOST_MEMORY_POOL_INIT
{
    char const                *PoolName;             /// A nul-terminated string specifying the name of the pool. This value is used for debugging purposes only.
    size_t                     PoolCapacity;         /// The maximum number of allocations that can be made from the pool.
    size_t                     MinAllocationSize;    /// The minimum number of bytes that can be associated with any individual allocation.
    size_t                     MinCommitIncrease;    /// The minimum number of bytes that memory commitment can increase by for each allocation from the pool.
};

/// @summary Define the data associated with a single host memory allocation. Each allocation corresponds to a VirtualAlloc call.
/// By default, each chunk ends with a single guard page that will trigger an access violation on read or write.
struct OS_HOST_MEMORY_ALLOCATION
{
    OS_HOST_MEMORY_POOL       *SourcePool;           /// The OS_HOST_MEMORY_POOL from which the chunk was allocated.
    OS_HOST_MEMORY_ALLOCATION *NextAllocation;       /// Used by the OS_HOST_MEMORY_POOL to maintain the free list. May be used by the application.
    uint8_t                   *BaseAddress;          /// The address of the first accessible byte.
    size_t                     BytesReserved;        /// The number of bytes of process address space reserved by this allocation, not including the guard page (if any).
    size_t                     BytesCommitted;       /// The number of bytes of process address space committed by this allocation. Always <= BytesReserved.
    uint32_t                   AllocationFlags;      /// One or more of OS_HOST_MEMORY_ALLOCATION_FLAGS.
};

/// @summary Represents a range of host-visible memory. This type is not specific to the host operating system.
struct OS_MEMORY_RANGE
{
    union
    {
        uint8_t        *HostAddress;                 /// The base address of the host-visible portion of the memory range.
        size_t          ByteOffset;                  /// The byte offset from the start of the allocated memory region.
    };
    size_t              SizeInBytes;                 /// The number of bytes in the memory range.
};

/// @summary Defines the state associated with an arena allocator.
struct OS_ARENA_ALLOCATOR
{
    size_t              NextOffset;                  /// The byte offset, relative to the start of the associated memory range, of the next free byte.
    size_t              SizeInBytes;                 /// The maximum offset value. NextOffset is always <= SizeInBytes.
};

/// @summary Define the set of information returned from a buddy allocator block query.
/// This type can be used regardless of whether the memory being managed is host or device memory.
struct OS_BUDDY_BLOCK_INFO
{
    uint32_t            LevelIndex;                  /// The zero-based index of the level at which the block was allocated, with level 0 being the largest level.
    uint32_t            BitIndex;                    /// The zero-based index of the bit that is set for blocks in this level.
    uint32_t            BlockSize;                   /// The size of the blocks in this level, in bytes.
    uint32_t            BlockCount;                  /// The maximum number of blocks in this level.
    uint32_t            IndexOffset;                 /// The offset used to transform an absolute index into a relative index.
    uint32_t            LeftAbsoluteIndex;           /// The absolute block index of the leftmost block of the buddy pair, either BlockAbsoluteIndex or BuddyAbsoluteIndex.
    uint32_t            BlockAbsoluteIndex;          /// The absolute block index of the input block.
    uint32_t            BuddyAbsoluteIndex;          /// The absolute block index of the buddy of the input block.
};

/// @summary Define the information used to look up the status of a block in the allocator merge index.
/// The merge index contains one bit per buddy-pair. The bit is clear if both blocks are free or both are allocated, and set if only one block is allocated.
/// This type can be used regardless of whether the memory being managed is host or device memory.
struct OS_BUDDY_BLOCK_MERGE_INFO
{
    uint32_t            WordIndex;                   /// The zero-based index of the uint32_t value in the OS_BUDDY_ALLOCATOR::MergeIndex field.
    uint32_t            Mask;                        /// The mask value used to test or manipulate the state of the bit.
};

/// @summary Define the information used to look up the status of a block in the allocator split index.
/// The split index contains one bit per block for each level not including the leaf level. The bit is set if the corresponding block has been split.
/// This type can be used regardless of whether the memory being managed is host or device memory.
struct OS_BUDDY_BLOCK_SPLIT_INFO
{
    uint32_t            WordIndex;                   /// The zero-based index of the uint32_t value in the OS_BUDDY_ALLOCATOR::SplitIndex field.
    uint32_t            Mask;                        /// The mask value used to test or manipulate the state of the bit.
};

/// @summary Define the data associated with a buddy allocator.
/// The buddy allocator divides a memory range into power-of-two sized chunks between a minimum and maximum size.
/// It supports a general-style allocation interface, including realloc functionality, and may be used for host or device memory.
/// See http://bitsquid.blogspot.com/2015/08/allocation-adventures-3-buddy-allocator.html
struct OS_BUDDY_ALLOCATOR
{   static size_t const MAX_LEVELS = 16;             /// The maximum number of powers of two that can separate AllocationSizeMin and AllocationSizeMax.
    size_t              AllocationSizeMin;           /// The size of the smallest memory block that can be returned by this allocator.
    size_t              AllocationSizeMax;           /// The size of the largest memory block that can be returned by this allocator.
    size_t              BytesReserved;               /// The number of bytes marked as reserved. These bytes can never be allocated to the application.
    uint8_t            *MetadataBase;                /// The base address of the metadata storage allocation.
    uint32_t           *FreeListData;                /// Storage for the free list arrays, allocated as a single contiguous block. There are 1 << LevelCount uint32_t values.
    uint32_t           *MergeIndex;                  /// An array of 1 << (LevelCount-1) bits with each bit storing the state of a pair of buddies.
    uint32_t           *SplitIndex;                  /// An array of 1 << (LevelCount-1) bits with each bit set if the block at bit index i has been set.
    uint32_t            Reserved;                    /// Reserved for future use. Set to zero.
    uint32_t            LevelCount;                  /// The total number of levels used by the allocator, with level 0 representing the largest level.
    uint32_t            LevelBits[MAX_LEVELS];       /// The zero-based index of the set bit for each level. LevelCount entries are valid.
    uint32_t            FreeCount[MAX_LEVELS];       /// The number of entries in the free list for each level. LevelCount entries are valid.
    uint32_t           *FreeLists[MAX_LEVELS];       /// Each of LevelCount entries points to an array of 1 << LevelIndex values specifying free block offsets for that level.
};

/// @summary Define the data used to initialize a buddy allocator.
struct OS_BUDDY_ALLOCATOR_INIT
{
    size_t              AllocationSizeMin;           /// The size of the smallest memory block that can be returned by this allocator.
    size_t              AllocationSizeMax;           /// The size of the largest memory block that can be returned by this allocator.
    size_t              BytesReserved;               /// The number of bytes marked as reserved. These bytes can never be allocated to the application.
};

/// @summary Define the data associated with an arena-style host memory allocator. 
struct OS_HOST_MEMORY_ARENA
{
    OS_MEMORY_RANGE     HostMemory;                  /// The OS_MEMORY_RANGE specifying the start and size of the host-visible memory.
    OS_ARENA_ALLOCATOR  Allocator;                   /// The OS_ARENA_ALLOCATOR maintaining the allocator state.
};

/// @summary Define the data associated with a buddy-style host memory allocator.
struct OS_HOST_MEMORY_ALLOCATOR
{
    OS_MEMORY_RANGE     HostMemory;                  /// The OS_MEMORY_RANGE specifying the start and size of the host-visible memory.
    OS_BUDDY_ALLOCATOR  Allocator;                   /// The OS_BUDDY_ALLOCATOR maintaining the allocator state.
};

/// @summary Alias type for a marker within a memory arena.
typedef uintptr_t       os_arena_marker_t;           /// The marker stores the value of the OS_ARENA_ALLOCATOR::NextOffset field at a given point in time.

/// @summary Define the CPU topology information for the local system.
struct OS_CPU_INFO
{
    size_t              NumaNodes;                   /// The number of NUMA nodes in the system.
    size_t              PhysicalCPUs;                /// The number of physical CPUs installed in the system.
    size_t              PhysicalCores;               /// The total number of physical cores in all CPUs.
    size_t              HardwareThreads;             /// The total number of hardware threads in all CPUs.
    size_t              ThreadsPerCore;              /// The number of hardware threads per physical core.
    char                VendorName[13];              /// The CPUID vendor string.
    char                PreferAMD;                   /// Set to 1 if AMD OpenCL implementations are preferred.
    char                PreferIntel;                 /// Set to 1 if Intel OpenCL implementations are preferred.
    char                IsVirtualMachine;            /// Set to 1 if the process is running in a virtual machine.
};

/// @summary Represents the user-facing identifier of a task within the task scheduler.
typedef uint32_t        os_task_id_t;                /// The task ID stores the thread that created the task and the task index.

/// @summary Define the signature for the callback function invoked when a task is executed.
/// @param task_id The task identifier for the task being executed.
/// @param args A pointer to the task-local data buffer used to store task parameters supplied when the task was defined.
/// @param env The execution environment for the task, which can be used for defining additional tasks or allocating memory.
typedef void          (*OS_TASK_ENTRYPOINT)(os_task_id_t task_id, void *args, struct OS_TASK_ENVIRONMENT *env);

/// @summary Define the data associated with the system task profiler. The task profiler can be used to emit:
/// Events: Used for point-in-time events such as worker thread startup and shutdown or task submission.
/// Spans : Used to represent a range of time such as the time between task submission and execution, or task start and finish.
struct OS_TASK_PROFILER
{
    CV_PROVIDER        *Provider;                    /// The Win32 OS Layer provider.
    CV_MARKERSERIES    *MarkerSeries;                /// The marker series. Each profiler instance has a separate marker series.
};

/// @summary Defines the data associated with a task profiler object used to track a time range.
struct OS_TASK_PROFILER_SPAN
{
    CV_SPAN            *CvSpan;                      /// The Concurrency Visualizer SDK object representing the time span.
};

/// @summary Define the data associated with a double-ended queue of ready-to-run task identifiers.
/// The thread that owns the queue can perform PUSH and TAKE operations; other threads can only perform STEAL operations.
#pragma warning(push)
#pragma warning(disable:4324)                        /// Structure was padded due to __declspec(align())
struct OS_CACHELINE_ALIGN OS_TASK_QUEUE
{   typedef std::atomic<int64_t>       atomic_s64_t; /// A signed 64-bit integer that can be read and written atomically.
    static size_t const PADDING_BYTES  = 56;         /// The number of bytes of padding required to separate public and private data and reduce cacheline contention.
    atomic_s64_t        Public;                      /// The public end of the deque, updated by STEAL operations.
    uint8_t             Pad0[PADDING_BYTES];         /// Padding separating the public and private ends of the queue.
    atomic_s64_t        Private;                     /// The private end of the deque, updated by PUSH and TAKE operations.
    uint8_t             Pad1[PADDING_BYTES];         /// Padding separating the private end and shared data.
    int64_t             Mask;                        /// The bitmask used to map the Public and Private indices into the storage array.
    os_task_id_t       *TaskIds;                     /// The identifiers of the ready-to-run tasks in the queue.
};
#pragma warning(pop)

/// @summary Define the data stored for a single task.
struct OS_CACHELINE_ALIGN OS_TASK_DATA
{   typedef std::atomic<int32_t>       atomic_s32_t; /// A signed 32-bit integer that can be read and written atomically.
    static size_t const MAX_DATA_BYTES = 48;         /// The maximum size of the per-task parameter data, in bytes.
    static size_t const MAX_PERMITS    = 14;         /// The maximum number of tasks that this task can permit to run.
    atomic_s32_t        WaitCount;                   /// The number of tasks that must complete before this task is ready-to-run.
    os_task_id_t        ParentId;                    /// The identifier of the parent task, or OS_INVALID_TASK_ID.
    OS_TASK_ENTRYPOINT  TaskMain;                    /// The task entry point, or NULL for external tasks.
    uint8_t             TaskData[MAX_DATA_BYTES];    /// The per-task parameter data.

    atomic_s32_t        WorkCount;                   /// The number of outstanding work items (this task, plus one for each child task.)
    atomic_s32_t        PermitCount;                 /// The number of tasks that this task permits to run (the number of valid entries in PermitIds.)
    os_task_id_t        PermitIds[MAX_PERMITS];      /// The task ID of each task permitted to run when this task completes.
};

/// @summary Define the data associated with a pre-allocated, fixed-size pool of tasks. Task pools are associated with a single thread.
struct OS_CACHELINE_ALIGN OS_TASK_POOL
{   typedef std::atomic<uint8_t>       atomic_u8_t;  /// An unsigned 8-bit integer that can be read and written atomically.
    atomic_u8_t        *SlotStatus;                  /// For each task slot in the pool, 0 if the slot is available or 1 if the slot is in-use.
    uint32_t            IndexMask;                   /// Bitmask used to map an index value into the task data array(s). This is the array size minus one.
    uint32_t            NextIndex;                   /// The zero-based index of the first slot to check when the next task is allocated from the pool.
    uint32_t            PoolIndex;                   /// The zero-based index of the pool within the scheduler's list of task pools.
    uint32_t            PoolUsage;                   /// One or more of OS_TASK_POOL_USAGE indicating whether the pool can be used to run tasks.
    uint32_t            ThreadId;                    /// The operating system identifier of the thread that owns the pool.
    int32_t             LastError;                   /// The error code reported by the last attempt to define a task on the pool.
    uint32_t            PoolId;                      /// The application-defined identifier of the associated pool type.
    uint16_t            NextWorker;                  /// The zero-based index of the next worker to notify.
    uint16_t            WorkerCount;                 /// The total number of worker threads in the scheduler thread pool.
    OS_TASK_POOL       *TaskPoolList;                /// A local pointer to the set of all task pools within the scheduler.
    OS_TASK_DATA       *TaskPoolData;                /// The buffer storing per-task data.
    OS_TASK_POOL       *NextFreePool;                /// Pointer to the next OS_TASK_POOL in the free list, or NULL if this pool is allocated.

    OS_TASK_QUEUE       WorkQueue;                   /// The work-stealing deque of task IDs that are ready-to-run.
};

/// @summary Define the data that might be needed by a thread when defining or executing tasks.
struct OS_TASK_ENVIRONMENT
{
    OS_TASK_PROFILER          *TaskProfiler;         /// The task profiler associated with the task scheduler.
    OS_TASK_SCHEDULER         *TaskScheduler;        /// The OS_TASK_SCHEDULER that owns the task pool.
    OS_TASK_POOL              *TaskPool;             /// The OS_TASK_POOL allocated to the thread.
    OS_CPU_INFO               *HostCpuInfo;          /// Information about the host CPU layout.
    uint32_t                   ThreadId;             /// The operating system identifier of the thread associated with the execution environment.
    uint32_t                   PoolUsage;            /// One or more of OS_TASK_POOL_USAGE indicating whether the pool can be used to run tasks.
    uintptr_t                  ContextData;          /// The opaque value passed through to each task and specified in the OS_TASK_SCHEDULER_INIT::TaskContextData field.
    OS_HOST_MEMORY_ARENA      *LocalMemory;          /// The thread-local memory arena used for temporary working space.
    OS_HOST_MEMORY_ARENA      *GlobalMemory;         /// The shared global memory arena used for persistent storage.
    OS_IO_THREAD_POOL         *IoThreadPool;         /// The application thread pool used for submitting asynchronous I/O requests.
    OS_IO_REQUEST_POOL        *IoRequestPool;        /// The OS_IO_REQUEST_POOL allocated to the thread.
};

/// @summary Define the data passed to a task scheduler worker thread during initialization.
/// This structure needs to be copied into thread local memory before signaling ready or error.
struct OS_TASK_SCHEDULER_THREAD_INIT
{
    OS_TASK_SCHEDULER         *TaskScheduler;        /// The OS_TASK_SCHEDULER that is creating the worker thread.
    OS_CPU_INFO                HostCpuInfo;          /// Information about the host CPU layout.
    HANDLE                     CompletionPort;       /// The I/O completion port used to notify the thread that work is available to steal.
    HANDLE                     ReadySignal;          /// A manual-reset event to signal when the thread has successfully completed initialization.
    HANDLE                     ErrorSignal;          /// A manual-reset event to signal when the thread has encountered a fatal error during initialization.
    uintptr_t                  TaskContextData;      /// The opaque value to be passed through to each task when it is executed.
    OS_IO_THREAD_POOL         *IoThreadPool;         /// The thread pool to use for executing I/O requests.
    uint32_t                   WorkerIndex;          /// The zero-based index of the worker thread.
    uint32_t                   PoolId;               /// The value used to identify the type of task pool to allocate during initialization.
};

/// @summary Define the data associated with a task scheduler. The task scheduler maintains several pools used to define tasks, along with a pool of worker threads dedicated to executing tasks.
struct OS_TASK_SCHEDULER
{
    size_t                     PoolTypeCount;        /// The number of task pool types defined within the scheduler.
    uint32_t                  *PoolIdList;           /// An array of PoolTypeCount items specifying the unique identifers for each task pool type.
    OS_TASK_POOL             **PoolFreeLists;        /// An array of PoolTypeCount pointers to OS_TASK_POOL representing the free list for each pool type.
    CRITICAL_SECTION          *PoolFreeListLocks;    /// An array of PoolTypeCount CRITICAL_SECTION objects protecting the free list for each pool type. 
    size_t                     TaskPoolCount;        /// The total number of OS_TASK_POOL objects created by the scheduler.
    OS_TASK_POOL              *TaskPoolList;         /// An array of TaskPoolCount OS_TASK_POOL objects representing all task pools (regardless of type) created by the scheduler.
    OS_HOST_MEMORY_ARENA      *TaskPoolArenas;       /// An array of TaskPoolCount OS_HOST_MEMORY_ARENA objects representing the thread-local memory arena allocated for each task pool.
    OS_IO_REQUEST_POOL        *TaskIoRequestPools;   /// An array of TaskPoolCount OS_IO_REQUEST_POOL objects representing the thread-local I/O request pool allocated for each task pool.
    size_t                     WorkerThreadCount;    /// The number of currently worker threads dedicated to executing tasks.
    unsigned int              *WorkerThreadIds;      /// An array of WorkerThreadCount values specifying the operating system thread identifier for each active worker thread.
    HANDLE                    *WorkerThreadHandle;   /// An array of WorkerThreadCount values specifying the operating system thread handle for each active worker thread.
    HANDLE                    *WorkerThreadReady;    /// An array of WorkerThreadCount values specifying the manual-reset event signaled by each active worker to indicate that it is ready to run.
    HANDLE                    *WorkerThreadError;    /// An array of WorkerThreadCount values specifying the manual-reset event signaled by each active worker to indicate a fatal error has occurred.
    HANDLE                    *WorkerThreadPort;     /// An array of WorkerThreadCount values specifying the I/O completion port used to wait and wake worker threads in the pool.

    OS_HOST_MEMORY_ARENA       GlobalMemoryArena;    /// The global memory arena.
    OS_IO_THREAD_POOL         *IoThreadPool;         /// The thread pool to use for executing I/O reqests.
    OS_CPU_INFO                HostCpuInfo;          /// Information about the host CPU.
    uintptr_t                  TaskContextData;      /// An opaque value to be passed through to each task when it executes.

    OS_TASK_PROFILER           TaskProfiler;         /// The task profiler associated with the thread pool.

    OS_HOST_MEMORY_ARENA       SchedulerArena;       /// The OS_HOST_MEMORY_ARENA used to sub-allocate from SchedulerMemory.
    OS_HOST_MEMORY_ALLOCATION *SchedulerMemory;      /// The memory allocation representing all memory allocated to the scheduler.
    OS_HOST_MEMORY_POOL       *SchedulerMemoryPool;  /// The pool from which the scheduler memory was allocated.
};

/// @summary Define the data used to configure a single type of task pool.
struct OS_TASK_POOL_INIT
{
    uint32_t                   PoolId;               /// Any value, unique (within the scheduler) value used to identify the pool type to the application.
    uint32_t                   PoolUsage;            /// One or more of OS_TASK_POOL_USAGE indicating whether the pool is used to define tasks, execute tasks, or both.
    size_t                     PoolCount;            /// The number of task pools of this type that should be created within the scheduler.
    size_t                     MaxIoRequests;        /// The size of the thread-local I/O request pool to to allocate for the task pool.
    size_t                     MaxActiveTasks;       /// The maximum number of tasks that can be defined within the pool at any given time.
    size_t                     LocalMemorySize;      /// The size of the local memory arena allocated for the task pool, in bytes. This value may be zero.
};

/// @summary Define the data used to configure a task scheduler.
struct OS_TASK_SCHEDULER_INIT
{
    OS_HOST_MEMORY_POOL       *SchedulerMemoryPool;  /// The pool from which host memory is allocated for scheduler global and local memory.
    size_t                     WorkerThreadCount;    /// The number of worker threads dedicated to executing tasks.
    size_t                     GlobalMemorySize;     /// The size of the global memory arena, in bytes. Global memory is shared between all task pools. This value may be zero.
    size_t                     PoolTypeCount;        /// The number of items in the TaskPoolTypes array.
    OS_TASK_POOL_INIT         *TaskPoolTypes;        /// An array of one or more OS_TASK_POOL_INIT structures used to define the task pools.
    OS_IO_THREAD_POOL         *IoThreadPool;         /// The thread pool to use for executing I/O requests.
    uintptr_t                  TaskContextData;      /// An opaque value to be passed through to each task when it executes.
};

/// @summary Define a scope-based object used for reporting the execution duration for a task.
/// Declare on the stack as the first thing in your task entrypoint, for example:
/// void MyTaskMain(os_task_id_t task_id, void *task_args, OS_TASK_ENVIRONMENT *taskenv) {
///     OS_TASK_SCOPE task_scope(__FUNCTION__, task_id, taskenv);
///     {
///         // do your work here
///     }
/// }
/// 
/// - or -
/// 
/// void MyTaskMain(os_task_id_t task_id, void *task_args, OS_TASK_ENVIRONMENT *taskenv) {
///     OS_PROFILE_TASK(task_id, taskenv);
///     // do your work here
/// }
/// This will cause a span to appear in Concurrency Visualizer profiling sessions named "MyTaskMain 1234ABCD".
struct OS_TASK_SCOPE
{
    OS_TASK_ENVIRONMENT  *Env;                       /// The OS_TASK_ENVIRONMENT associated with the calling thread.
    OS_TASK_PROFILER_SPAN Span;                      /// The Concurrency Visualizer SDK object representing the time span.
    inline OS_TASK_SCOPE(char const *name, os_task_id_t task_id, OS_TASK_ENVIRONMENT *taskenv)
        : 
        Env(taskenv)
    {
        UNREFERENCED_PARAMETER(name);                // For builds that #define OS_DISABLE_TASK_PROFILER
        UNREFERENCED_PARAMETER(task_id);             // For builds that #define OS_DISABLE_TASK_PROFILER
        OsTaskSpanEnter(taskenv, Span, "%S %08X", name, task_id);
    }
    inline ~OS_TASK_SCOPE(void)
    {
        OsTaskSpanLeave(Env, Span);
    }
};
#ifndef OS_PROFILE_TASK
    #ifdef  OS_DISABLE_TASK_PROFILER
        #define OS_PROFILE_TASK(id, env)    
    #else
        #define OS_PROFILE_TASK(id, env)    OS_TASK_SCOPE __cv_task_scope__(__FUNCTION__, (id), (env))
    #endif
#endif

/// @summary Define the data associated with a fence task, which can be used to put an OS thread into a wait state until one or more tasks have completed.
struct OS_TASK_FENCE
{
    HANDLE                FenceSignal;               /// The manual-reset event signaled when all of the fence task dependencies have completed.
};

/// @summary Define the data available to an application callback executing on a worker thread.
struct OS_WORKER_THREAD
{
    OS_THREAD_POOL       *ThreadPool;                /// The thread pool that manages the worker.
    OS_HOST_MEMORY_ARENA *ThreadArena;               /// The thread-local memory arena.
    HANDLE                CompletionPort;            /// The I/O completion port used to wait and wake the thread.
    void                 *PoolContext;               /// The opaque, application-specific data passed through to the thread.
    void                 *ThreadContext;             /// The opaque, application-specific data created by the OS_WORKER_INIT callback for the thread.
    size_t                ArenaSize;                 /// The size of the thread-local memory arena, in bytes.
    unsigned int          ThreadId;                  /// The operating system identifier for the thread.
};

/// @summary Define the signature for the callback invoked during worker thread initialization to allow the application to create any per-thread resources.
/// @param thread_args An OS_WORKER_THREAD instance specifying worker thread data. The callback should set the ThreadContext field to its private data.
/// @return Zero if initialization was successful, or -1 to terminate the worker thread.
typedef int             (*OS_WORKER_INIT)(OS_WORKER_THREAD *thread_args);

/// @summary Define the signature for the callback representing the application entry point on a worker thread.
/// @param thread_args An OS_WORKER_THREAD instance, valid until the OS_WORKER_ENTRY returns, specifying per-thread data.
/// @param signal_arg An application-defined value specified with the wake notification.
/// @param wake_reason One of OS_WORKER_THREAD_WAKE_REASON indicating the reason the thread was woken.
typedef void            (*OS_WORKER_ENTRY)(OS_WORKER_THREAD *thread_args, uintptr_t signal_arg, int wake_reason);

/// @summary Define the data package passed to OsWorkerThreadMain during thread pool creation.
struct OS_WORKER_THREAD_INIT
{
    OS_THREAD_POOL     *ThreadPool;                  /// The thread pool that manages the worker thread.
    HANDLE              ReadySignal;                 /// The manual-reset event to be signaled by the worker when it has successfully completed initialization and is ready-to-run.
    HANDLE              ErrorSignal;                 /// The manual-reset event to be signaled by the worker if it encounters a fatal error during initialization.
    HANDLE              LaunchSignal;                /// The manual-reset event to be signaled by the pool when all worker threads should begin running.
    HANDLE              TerminateSignal;             /// The manual-reset event to be signaled by the pool when all worker threads should terminate.
    HANDLE              CompletionPort;              /// The I/O completion port signaled when the worker thread should wake up.
    OS_WORKER_INIT      ThreadInit;                  /// The callback function to run during thread launch to allow the application to allocate or initialize any per-thread data.
    OS_WORKER_ENTRY     ThreadMain;                  /// The callback function to run when a message is received by the worker thread.
    void               *PoolContext;                 /// Opaque application-supplied data to pass through to AppThreadMain.
    size_t              StackSize;                   /// The stack size of the worker thread, in bytes, or OS_WORKER_THREAD_STACK_DEFAULT.
    size_t              ArenaSize;                   /// The size of the thread-local memory arena, in bytes.
    uint32_t            NUMAGroup;                   /// The zero-based index of the NUMA processor group on which the worker thread will be scheduled.
};

/// @summary Define the data maintained by a pool of worker threads.
struct OS_THREAD_POOL
{
    size_t              ActiveThreads;               /// The number of currently active threads in the pool.
    unsigned int       *OSThreadIds;                 /// The operating system thread identifier for each active worker thread.
    HANDLE             *OSThreadHandle;              /// The operating system thread handle for each active worker thread.
    HANDLE             *WorkerReady;                 /// The manual-reset event signaled by each active worker to indicate that it is ready to run.
    HANDLE             *WorkerError;                 /// The manual-reset event signaled by each active worker to indicate a fatal error has occurred.
    HANDLE              CompletionPort;              /// The I/O completion port used to wait and wake worker threads in the pool.
    HANDLE              LaunchSignal;                /// The manual-reset event used to launch all threads in the pool.
    HANDLE              TerminateSignal;             /// The manual-reset event used to notify all threads that they should terminate.
    OS_TASK_PROFILER    TaskProfiler;                /// The task profiler associated with the thread pool.
};

/// @summary Define the parameters used to configure a thread pool.
struct OS_THREAD_POOL_INIT
{
    OS_WORKER_INIT      ThreadInit;                  /// The callback function to run during launch for each worker thread to initialize thread-local resources.
    OS_WORKER_ENTRY     ThreadMain;                  /// The callback function to run on the worker thread(s) when a signal is received.
    void               *PoolContext;                 /// Opaque application-supplied data to be passed to OS_WORKER_INIT for each worker thread.
    size_t              ThreadCount;                 /// The number of worker threads to create.
    size_t              StackSize;                   /// The stack size for each worker thread, in bytes, or OS_WORKER_THREAD_STACK_DEFAULT.
    size_t              ArenaSize;                   /// The size of the per-thread memory arena, in bytes.
    uint32_t            NUMAGroup;                   /// The zero-based index of the NUMA processor group on which the worker threads will be scheduled. Set to 0.
};

/// @summary Define the information maintained in-memory for a file known to the file system.
struct OS_FILE_INFO
{   static size_t const MAX_STRING_BYTES = 16;       /// The maximum number of bytes of string data (including the zero terminator) stored for filename and extension data.
    int64_t             FileSize;                    /// The size of the file data, in bytes. 
    int64_t             BaseOffset;                  /// The offset of the file data from the start of the file (for files contained within archives).
    FILETIME            LastWrite;                   /// The last write time of the file.
    DWORD               Attributes;                  /// The file attributes, as would be returned by GetFileAttributes().
    char                FileName [MAX_STRING_BYTES]; /// A zero-terminated UTF-8 string containing (up to) 15 bytes of filename data. This data is used for hash collision resolution.
    char                Extension[MAX_STRING_BYTES]; /// A zero-terminated ASCII string containing (up to) the first 15 bytes of file extension data. This data is used for 
};

/// @summary Define the data stored with a fixed-size chunk of file information.
struct OS_FILE_INFO_CHUNK
{   static size_t const CAPACITY = 128;              /// The maximum number of file information entries stored in a single chunk.
    SRWLOCK             RWLock;                      /// The reader-writer lock protecting the chunk contents.
    OS_FILE_INFO_CHUNK *NextChunk;                   /// The next chunk in the list, or NULL if this is the last chunk in the block.
    uint16_t            RecordCount;                 /// The number of items used within this chunk.
    uint32_t            PathHash[CAPACITY];          /// The 32-bit hash of the absolute path of each file within the chunk.
    OS_FILE_INFO        FileInfo[CAPACITY];          /// Information about each file contained within the chunk.
};

/// @summary Define the data associated with an allocator for OS_FILE_INFO_CHUNK instances. Chunks are recycled using a free list.
struct OS_FSIC_ALLOCATOR
{
    CRITICAL_SECTION      AllocatorLock;             /// Critical section held for the duration of an allocation or free operation.
    OS_HOST_MEMORY_ARENA *MemoryArena;               /// The memory arena used for allocating new chunks if the free list is empty.
    OS_FILE_INFO_CHUNK   *FreeList;                  /// The first chunk in the free list, or NULL if the free list is empty.
};

/// @summary Define the data maintained with a memory-mapped file opened for read access.
struct OS_FILE_MAPPING
{
    HANDLE              Filedes;                     /// A valid file handle, or INVALID_HANDLE_VALUE.
    HANDLE              Filemap;                     /// A valid file mapping handle, or NULL.
    int64_t             FileSize;                    /// The size of the file, in bytes, at the time it was opened.
    size_t              Granularity;                 /// The system allocation granularity, in bytes.
};

/// @summary Define the data associated with a region of a file loaded or mapped into memory.
struct OS_FILE_DATA
{
    uint8_t            *Buffer;                      /// The buffer containing the loaded file data.
    void               *MapPtr;                      /// The address returned by MapViewOfFile.
    int64_t             Offset;                      /// The offset of the data in this region from the start of the file, in bytes.
    int64_t             DataSize;                    /// The number of bytes in Buffer that are valid.
    uint32_t            Flags;                       /// One or more of OS_FILE_DATA_FLAGS describing the allocation attributes of the OS_FILE_DATA.
};

/// @summary Define the data used to execute a low-level I/O operation. Not all data is used by all operations.
struct OS_IO_OPERATION
{
    WCHAR              *FilePath;                    /// Pointer to a zero-terminated string specifying the file path. Used for OPEN operations only.
    HANDLE              FileHandle;                  /// The file handle specifying the target for the operation. Used for READ, WRITE, FLUSH and CLOSE operations.
    HANDLE              CompletionPort;              /// The I/O completion port to associate with the file, or NULL. Used for OPEN operations only.
    OVERLAPPED         *Overlapped;                  /// The OVERLAPPED object to use for asynchronous I/O, or NULL to use synchronous I/O. Used for READ and WRITE operations.
    void               *DataBuffer;                  /// The source buffer specifying data to write, or destination buffer for data being read. Used for READ and WRITE operations.
    int64_t             FileOffset;                  /// The absolute byte offset within the file at which to perform the I/O operation. Used for asynchronous READ and WRITE only.
    int64_t             PreallocationSize;           /// The desired size of the file, if OS_IO_HINT_FLAG_PREALLOCATE is specified. Used for OPEN operations only.
    uint32_t            TransferAmount;              /// The number of bytes to transfer. Used for READ and WRITE operations.
    uint32_t            IoHintFlags;                 /// One or more of OS_IO_HINT_FLAGS. Used for OPEN operations only.
};

/// @summary Define the data resulting from the execution of a low-level I/O operation.
struct OS_IO_OPERATION_RESULT
{
    HANDLE              FileHandle;                  /// The file handle specifying the target for the operaiton. For OPEN operations, this is the handle of the opened file.
    DWORD               ResultCode;                  /// The operating system result code.
    uint32_t            TransferAmount;              /// The number of bytes transferred.
    bool                CompletedSynchronously;      /// Set to true if the I/O operation completed synchronously, or false if the I/O operation completed asynchronously.
    bool                WasSuccessful;               /// Set to true if the I/O operation completed successfully, or false if the I/O operation failed.
};

/// @summary Define execution environment data passed back to an I/O completion callback.
/// This data can be used to allocate and initialize a chained request, or submit a new request.
struct OS_IO_REQUEST_CONTEXT
{
    OS_IO_REQUEST_POOL *RequestPool;                 /// The I/O request pool from which the I/O request was allocated.
    OS_IO_THREAD_POOL  *ThreadPool;                  /// The thread pool that executed the I/O request.
    uintptr_t           PoolContext;                 /// Opaque application-defined data associated with the I/O thread pool.
    uintptr_t           ThreadContext;               /// Opaque application-defined data associated with the thread that executed the request.
};

/// @summary Data specifying profiling data for an I/O operation.
struct OS_IO_PROFILE
{
    uint64_t            QueueDelay;                  /// The time, in nanoseconds, between when the I/O request was submitted and when it started executing.
    uint64_t            ExecutionTime;               /// The time, in nanoseconds, that the I/O request spent executing.
    uint32_t            OsThreadId;                  /// The operating system identifier of the worker thread that executed the request.
};

/// @summary Define the signature for the function invoked when an I/O operation has completed. These callbacks run on the I/O thread pool thread, and should perform a minimal amount of non-blocking work.
/// @param was_successful Set to true if the request executed successfully.
/// @param result Data describing the result of the I/O operation that was executed.
/// @param context Specifies the execution environment for the I/O operation.
/// @param profile Specifies the execution timing information for the I/O operation.
/// @return A chained I/O request to execute immediately, or NULL.
typedef OS_IO_REQUEST* (*OS_IO_COMPLETE)(bool was_successful, OS_IO_RESULT *result, OS_IO_REQUEST_CONTEXT *context, OS_IO_PROFILE *profile);

/// @summary Define the data representing an active request internally within the background I/O system.
struct OS_IO_REQUEST
{
    OS_IO_REQUEST      *NextRequest;                 /// Pointer to the next node in the list, or NULL if this is the tail node.
    OS_IO_REQUEST      *PrevRequest;                 /// Pointer to the previous node in the list, or NULL if this is the head node.
    OS_IO_REQUEST_POOL *RequestPool;                 /// The OS_IO_REQUEST_POOL from which this request was allocated.
    int                 RequestType;                 /// One of OS_IO_REQUEST_TYPE specifying the type of operation being performed.
    int                 RequestState;                /// One of OS_IO_REQUEST_STATE specifying the current state of the request.
    OVERLAPPED          Overlapped;                  /// The OVERLAPPED instance associated with the asynchronous request.
    HANDLE              FileHandle;                  /// The file handle associated with the request.
    WCHAR              *PathBuffer;                  /// Pointer to a caller-managed buffer specifying the path of the file to OPEN.
    void               *DataBuffer;                  /// The caller-managed buffer from which to READ or WRITE data.
    int64_t             DataAmount;                  /// The number of bytes to transfer to or from the caller-managed data buffer.
    int64_t             BaseOffset;                  /// The byte offset of the start of the operation from the start of the physical file.
    int64_t             FileOffset;                  /// The byte offset of the start of the operation from the start of the logical file.
    uintptr_t           UserContext;                 /// Opaque data associated with the request to be passed through to the completion callback.
    OS_IO_COMPLETE      IoComplete;                  /// The callback to invoke when the operation has completed. May be NULL.
    uint64_t            IoSubmitTime;                /// The timestamp (in ticks) at which the I/O request was submitted.
    uint64_t            IoLaunchTime;                /// The timestamp (in ticks) at which the I/O request was dequeued by a worker thread and began executing.
    uint64_t            IoFinishTime;                /// The timestamp (in ticks) at which the I/O request finished executing.
    uint32_t            IoHintFlags;                 /// A combination of one or more OS_IO_HINT_FLAGS. Generally only used for OPEN operations.
};

/// @summary Define an object managing a preallocated pool of I/O requests.
/// The I/O request pool may be accessed concurrently by a submitting thread and a worker thread.
struct OS_IO_REQUEST_POOL
{
    CRITICAL_SECTION    ListLock;                    /// The lock protecting the live and free lists.
    OS_IO_REQUEST      *LiveRequest;                 /// Pointer to the first live request, or NULL if no requests have been allocated from the pool.
    OS_IO_REQUEST      *FreeRequest;                 /// Pointer to the first free request, or NULL if no requests are available in the pool.
    OS_IO_REQUEST      *NodePool;                    /// Pointer to the pool allocation.
};

/// @summary Define the data returned from a background I/O request submitted by the application.
struct OS_IO_RESULT
{
    int                 RequestType;                 /// One of OS_IO_REQUEST_TYPE specifying the type of operation that completed.
    uint32_t            ResultCode;                  /// ERROR_SUCCESS or another result code indicating whether the operation completed successfully.
    uintptr_t           UserContext;                 /// Opaque data associated with the request by the application.
    HANDLE              FileHandle;                  /// The handle of the file associated with the I/O request. This value may be INVALID_HANDLE_VALUE.
    WCHAR              *PathBuffer;                  /// The path of the file associated with the I/O request. This value may be NULL.
    void               *DataBuffer;                  /// The source or destination caller-managed buffer. This value may be NULL.
    union
    {
        int64_t         FileSize;                    /// For an OPEN operation, the file size is returned in this field.
        int64_t         DataAmount;                  /// The number of bytes read from or written to the data buffer.
    };
    union
    {
        int64_t         BaseOffset;                  /// The byte offset of the start of the operation from the start of the physical file.
        int64_t         PhysicalSectorSize;          /// For an OPEN operation, the physical device sector size in bytes is returned in this field.
    };
    int64_t             FileOffset;                  /// The byte offset of the start of the operation from the start of the logical file.
};

/// @summary Define the signature for the callback function invoked for and from each worker thread in an I/O thread pool.
/// The application should use this function to create any per-thread data to be passed back in the OS_IO_REQUEST_CONTEXT::ThreadContext field.
/// @param io_pool The I/O thread pool that owns the worker thread.
/// @param pool_context Opaque data provided by the application and associated with the I/O thread pool.
/// @param thread_id The operating system identifier of the worker thread that is initializing.
/// @param thread_context On return, the application should store any opaque application data in this location.
/// @return Zero if initialization completes successfully, or -1 if initialization has failed.
typedef int           (*OS_IO_WORKER_INIT)(OS_IO_THREAD_POOL *io_pool, uintptr_t pool_context, uint32_t thread_id, uintptr_t *thread_context);

/// @summary Define the data passed to an I/O worker thread on startup. The thread should copy the data into thread-local memory.
struct OS_IO_THREAD_INIT
{
    OS_IO_THREAD_POOL  *ThreadPool;                  /// The OS_IO_THREAD_POOL to which the worker belongs.
    OS_IO_WORKER_INIT   ThreadInit;                  /// The callback function to invoke from each worker thread at startup time to create any thread-local 
    HANDLE              ReadySignal;                 /// A manual-reset event to be signaled by the worker when it has successfully completed initialization and is ready-to-run.
    HANDLE              ErrorSignal;                 /// A manual-reset event to be signaled by the worker before it terminates when it encounters a fatal error.
    HANDLE              TerminateSignal;             /// A manual-reset event to be signaled by the application when the worker should terminate.
    HANDLE              CompletionPort;              /// The I/O completion port to be monitored by the thread for incoming events.
    uintptr_t           PoolContext;                 /// Opaque data specified by the application.
};

/// @summary Define the data associated with an I/O thread pool. The I/O thread pool differs from a standard worker thread pool; all threads launch immediately and have only 64KB of stack space.
struct OS_IO_THREAD_POOL
{
    size_t              ActiveThreads;               /// The number of I/O worker threads in the pool.
    unsigned int       *OSThreadIds;                 /// An array of operating system thread identifiers.
    HANDLE             *OSThreadHandle;              /// An array of operating system thread handles.
    HANDLE             *WorkerReady;                 /// An array of manual-reset events used by each thread to signal successful initialization.
    HANDLE             *WorkerError;                 /// An array of manual-reset events used by each thread to signal fatal errors prior to termination.
    HANDLE              CompletionPort;              /// The I/O completion port used to signal worker threads.
    HANDLE              TerminateSignal;             /// A manual-reset event used to signal all worker threads to terminate.
};

/// @summary Define the data used by the application to configure an I/O thread pool.
struct OS_IO_THREAD_POOL_INIT
{
    OS_IO_WORKER_INIT   ThreadInit;                  /// The callback to invoke for each worker thread in the pool.
    size_t              ThreadCount;                 /// The number of threads in the pool. 
    uintptr_t           PoolContext;                 /// Opaque data associated with the pool.
};

/// @summary Define the data used to represent the various parts of a native path string.
struct OS_PATH_PARTS
{
    WCHAR              *Root;                        /// Pointer to the first character of the root, share or drive portion of the path.
    WCHAR              *RootEnd;                     /// Pointer to the last character of the root, share or drive portion of the path.
    WCHAR              *Path;                        /// Pointer to the first character of the directory portion of the path.
    WCHAR              *PathEnd;                     /// Pointer to the last character of the directory portion of the path.
    WCHAR              *Filename;                    /// Pointer to the first character of the filename portion of the path.
    WCHAR              *FilenameEnd;                 /// Pointer to the last character of the filename portion of the path.
    WCHAR              *Extension;                   /// Pointer to the first character of the extension portion of the path.
    WCHAR              *ExtensionEnd;                /// Pointer to the last character of the extension portion of the path.
    uint32_t            PathFlags;                   /// One or more of OS_PATH_FLAGS specifying attributes of the path.
};

/// @summary Define the data associated with keyboard state.
struct OS_KEYBOARD_STATE
{
    uint32_t            KeyState[8];                 /// A bitvector (256 bits) mapping scan code to key state (1 = key down.)
};

/// @summary Define the data associated with gamepad state (Xbox controller.)
struct OS_GAMEPAD_STATE
{
    uint32_t            LTrigger;                    /// The left trigger value, in [0, 255].
    uint32_t            RTrigger;                    /// The right trigger value, in [0, 255].
    uint32_t            Buttons;                     /// A bitvector storing up to 32 button states (1 = button down.)
    float               LStick[4];                   /// The left analog stick X, Y, magnitude and normalized magnitude values, after deadzone logic is applied.
    float               RStick[4];                   /// The right analog stick X, Y, magnitude and normalized magnitude values, after deadzone logic is applied.
};

/// @summary Define the data associated with a pointing device (like a mouse.)
struct OS_POINTER_STATE
{
    int32_t             Pointer[2];                  /// The absolute X and Y coordinates of the pointer, in virtual display space.
    int32_t             Relative[3];                 /// The high definition relative X, Y and Z (wheel) values of the pointer. X and Y are specified in mickeys.
    uint32_t            Buttons;                     /// A bitvector storing up to 32 button states (0 = left, 1 = right, 2 = middle) (1 = button down.)
    uint32_t            Flags;                       /// Bitflags indicating postprocessing that needs to be performed.
};

/// @summary Define the data associated with a list of user input devices of the same type.
template <typename T>
struct OS_INPUT_DEVICE_LIST
{   static size_t const MAX_DEVICES = OS_MAX_INPUT_DEVICES;
    size_t              DeviceCount;                 /// The number of attached devices.
    HANDLE              DeviceHandle[MAX_DEVICES];   /// The OS device handle for each device.
    T                   DeviceState [MAX_DEVICES];   /// The current state for each device.
};
typedef OS_INPUT_DEVICE_LIST<OS_KEYBOARD_STATE>      OS_KEYBOARD_LIST;
typedef OS_INPUT_DEVICE_LIST<OS_GAMEPAD_STATE>       OS_GAMEPAD_LIST;
typedef OS_INPUT_DEVICE_LIST<OS_POINTER_STATE>       OS_POINTER_LIST;

/// @summary Defines the data associated with a Raw Input device membership set computed from two device list snapshots.
struct OS_INPUT_DEVICE_SET
{   static size_t const MAX_DEVICES = OS_MAX_INPUT_DEVICES;
    size_t              DeviceCount;                 /// The number of devices in the device set.
    HANDLE              DeviceIds [MAX_DEVICES*2];   /// The Win32 device identifiers. There are no duplicates in the list.
    uint32_t            Membership[MAX_DEVICES*2];   /// The OS_INPUT_DEVICE_SET_MEMBERSHIP values for each device.
    uint8_t             PrevIndex [MAX_DEVICES*2];   /// The zero-based indices of the device in the previous device list.
    uint8_t             CurrIndex [MAX_DEVICES*2];   /// The zero-based indices of the device in the current device list.
};

/// @summary Define the data used to report events generated by a keyboard device between two state snapshots.
struct OS_KEYBOARD_EVENTS
{   static size_t const MAX_KEYS = 8;                /// The maximum number of key events reported.
    size_t              DownCount;                   /// The number of keys currently in the down state.
    size_t              PressedCount;                /// The number of keys just pressed.
    size_t              ReleasedCount;               /// The number of keys just released.
    uint8_t             Down[MAX_KEYS];              /// The virtual key codes for all keys currently down.
    uint8_t             Pressed[MAX_KEYS];           /// The virtual key codes for all keys just pressed.
    uint8_t             Released[MAX_KEYS];          /// The virtual key codes for all keys just released.
};

/// @summary Define the data used to report events generated by a pointer device between two state snapshots.
struct OS_POINTER_EVENTS
{   static size_t const MAX_BUTTONS = 8;             /// The maximum number of button events reported.
    int32_t             Cursor[2];                   /// The absolute position of the cursor in virtual display space.
    int32_t             Mickeys[2];                  /// The relative movement of the pointer from the last update, in mickeys.
    int32_t             WheelDelta;                  /// The mouse wheel delta from the last update.
    size_t              DownCount;                   /// The number of buttons currently in the pressed state.
    size_t              PressedCount;                /// The number of buttons just pressed.
    size_t              ReleasedCount;               /// The number of buttons just released.
    uint16_t            Down[MAX_BUTTONS];           /// The MK_nBUTTON identifiers for all buttons in the down state.
    uint16_t            Pressed[MAX_BUTTONS];        /// The MK_nBUTTON identifiers for all buttons that were just pressed.
    uint16_t            Released[MAX_BUTTONS];       /// The MK_nBUTTON identifiers for all buttons that were just released.
};

/// @summary Define the data used to report events generated by an XInput gamepad device between two state snapshots.
struct OS_GAMEPAD_EVENTS
{   static size_t const MAX_BUTTONS = 8;             /// The maximum number of button events reported.
    float               LeftTrigger;                 /// The left trigger value, in [0, 255].
    float               RightTrigger;                /// The right trigger value, in [0, 255].
    float               LeftStick[2];                /// The left analog stick normalized X and Y.
    float               LeftStickMagnitude;          /// The normalized magnitude of the left stick vector.
    float               RightStick[2];               /// The right analog stick normalized X and Y.
    float               RightStickMagnitude;         /// The normalized magnitude of the right stick vector.
    size_t              DownCount;                   /// The number of buttons currently in the pressed state.
    size_t              PressedCount;                /// The number of buttons just pressed.
    size_t              ReleasedCount;               /// The number of buttons just released.
    uint16_t            Down[MAX_BUTTONS];           /// The XINPUT_GAMEPAD_x identifiers for all buttons in the down state.
    uint16_t            Pressed[MAX_BUTTONS];        /// The XINPUT_GAMEPAD_x identifiers for all buttons that were just pressed.
    uint16_t            Released[MAX_BUTTONS];       /// The XINPUT_GAMEPAD_x identifiers for all buttons that were just released.
};

/// @summary Define the data used to report input events for all input devices attached to the system at a given point in time.
struct OS_INPUT_EVENTS
{   static size_t const MAX_DEVICES = OS_MAX_INPUT_DEVICES;
    size_t              KeyboardAttachCount;         /// The number of keyboard devices newly attached during the tick.
    HANDLE              KeyboardAttach[MAX_DEVICES]; /// The system device identifiers of the newly attached keyboard devices.
    size_t              KeyboardRemoveCount;         /// The number of keyboard devices newly removed during the tick.
    HANDLE              KeyboardRemove[MAX_DEVICES]; /// The system device identifiers of the newly removed keyboard devices.
    size_t              KeyboardCount;               /// The number of keyboard devices for which input events are reported.
    HANDLE              KeyboardIds[MAX_DEVICES];    /// The system device identifiers of the keyboards for which input events are reported.
    OS_KEYBOARD_EVENTS  KeyboardEvents[MAX_DEVICES]; /// The input event data for keyboard devices.
    size_t              PointerAttachCount;          /// The number of pointer devices newly attached during the tick.
    HANDLE              PointerAttach[MAX_DEVICES];  /// The system device identifiers of the newly attached pointer devices.
    size_t              PointerRemoveCount;          /// The number of pointer devices newly removed during the tick.
    HANDLE              PointerRemove[MAX_DEVICES];  /// The system device identifiers of the newly removed pointer devices.
    size_t              PointerCount;                /// The number of pointer devices for which input events are reported.
    HANDLE              PointerIds[MAX_DEVICES];     /// The system device identifiers of the pointer devices for which input events are reported.
    OS_POINTER_EVENTS   PointerEvents[MAX_DEVICES];  /// The input event data for pointer devices.
    size_t              GamepadAttachCount;          /// The number of gamepad devices newly attached during the tick.
    DWORD               GamepadAttach[MAX_DEVICES];  /// The gamepad port indices of the newly attached gamepad devices.
    size_t              GamepadRemoveCount;          /// The number of gamepad devices newly removed during the tick.
    DWORD               GamepadRemove[MAX_DEVICES];  /// The gamepad port indices of the newly removed gamepad devices.
    size_t              GamepadCount;                /// The number of gamepad devices for which input events are reported.
    DWORD               GamepadIds[MAX_DEVICES];     /// The gamepad port indices of the gamepad devices for which input events are reported.
    OS_GAMEPAD_EVENTS   GamepadEvents[MAX_DEVICES];  /// The input event data for gamepad devices.
};

/// @summary Define the data associated with the low-level input system.
struct OS_INPUT_SYSTEM
{
    uint64_t            LastPollTime;                /// The timestamp value at the last poll of all gamepad ports.
    uint32_t            PrevPortIds;                 /// Bitvector for gamepad ports connected on the previous tick.
    uint32_t            CurrPortIds;                 /// Bitvector for gamepad ports connected on the current tick.
    size_t              BufferIndex;                 /// Used to identify the "current" device state buffer.
    OS_KEYBOARD_LIST    KeyboardBuffer[2];           /// Identifier and state information for keyboard devices.
    OS_POINTER_LIST     PointerBuffer [2];           /// Identifier and state information for pointer devices.
    OS_GAMEPAD_LIST     GamepadBuffer [2];           /// Identifier and state information for gamepad devices.
};

/// @summary Define the data associated with a Vulkan Installable Client Driver (ICD).
struct OS_VULKAN_ICD_INFO
{
    WCHAR              *ManifestPath;                /// Pointer to the zero-terminated path of the ICD JSON manifest file.
    uint8_t            *ManifestData;                /// A zero-terminated buffer containing the contents of the JSON manifest file.
    WCHAR              *DriverPath;                  /// A zero-terminated buffer containing the path of the installable client driver library.
    uint32_t            MajorVersion;                /// The major version of the Vulkan API supported by the ICD.
    uint32_t            MinorVersion;                /// The minor version of the Vulkan API supported by the ICD.
    uint32_t            PatchVersion;                /// The patch version of the Vulkan API supported by the ICD.
};

/// @summary Defines the data associated with the top-level Vulkan entry points used to load the Vulkan API.
struct OS_VULKAN_RUNTIME_DISPATCH
{
    HMODULE                           LoaderHandle;                                  /// The module base address of the Vulkan loader or ICD.
    OS_LAYER_VULKAN_ICD_FUNCTION     (vkGetInstanceProcAddr);                        /// The vkGetInstanceProcAddr function.
    OS_LAYER_VULKAN_GLOBAL_FUNCTION  (vkCreateInstance);                             /// The vkCreateInstance function.
    OS_LAYER_VULKAN_GLOBAL_FUNCTION  (vkEnumerateInstanceLayerProperties);           /// The vkEnumerateInstanceLayerProperties function.
    OS_LAYER_VULKAN_GLOBAL_FUNCTION  (vkEnumerateInstanceExtensionProperties);       /// The vkEnumerateInstanceExtensionProperties function.
};

/// @summary Define the data returned by the top-level Vulkan runtime. The application uses this data to decide which instance-level layers and extensions to enable.
struct OS_VULKAN_RUNTIME_PROPERTIES
{
    size_t                            LayerCount;                                    /// The number of instance-level validation layers provided by the runtime.
    VkLayerProperties                *LayerProperties;                               /// An array where each element [Layer] specifies basic name and version information about a validation layer provided by the runtime.
    size_t                           *LayerExtensionCount;                           /// An array where each element [Layer] specifies the number of extensions exposed by the runtime for each instance-level validation layer.
    VkExtensionProperties           **LayerExtensionProperties;                      /// An array where each element [Layer][Extension] specifies the name and version information about a validation layer extension provided by the runtime.
    size_t                            ExtensionCount;                                /// The number of instance-level extensions provided by the runtime.
    VkExtensionProperties            *ExtensionProperties;                           /// An array where each element [Extension] specifies the name and version information about an instance-level extension exposed by the runtime.
};

/// @summary Defines the instance-level Vulkan runtime functions used to query physical device information and including any extension functions enabled by the application.
struct OS_VULKAN_INSTANCE_DISPATCH
{
    VkInstance                        InstanceHandle;                                /// The Vulkan instance used by the application to interface with the Vulkan API.

    OS_LAYER_VULKAN_GLOBAL_FUNCTION  (vkGetInstanceProcAddr);                        /// The vkGetInstanceProcAddr function, copied from the OS_VULKAN_RUNTIME_DISPATCH object.

    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkCreateDevice);                               /// The vkCreateDevice function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkDestroyInstance);                            /// The vkDestroyInstance function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkEnumeratePhysicalDevices);                   /// The vkEnumeratePhysicalDevices function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkEnumerateDeviceExtensionProperties);         /// The vkEnumerateDeviceExtensionProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetDeviceProcAddr);                          /// The vkGetDeviceProcAddr function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceFeatures);                  /// The vkGetPhysicalDeviceFeatures function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceFormatProperties);          /// The vkGetPhysicalDeviceFormatProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceImageFormatProperties);     /// The vkGetPhysicalDeviceImageFormatProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceMemoryProperties);          /// The vkGetPhysicalDeviceMemoryProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties);                /// The vkGetPhysicalDeviceProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties);     /// The vkGetPhysicalDeviceQueueFamilyProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceSparseImageFormatProperties);///The vkGetPhysicalDeviceSparseImageFormatProperties function.

    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkDestroySurfaceKHR);                          /// The VK_KHR_surface vkDestroySurfaceKHR function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceSupportKHR);         /// The VK_KHR_surface vkGetPhysicalDeviceSurfaceSupportKHR function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR);         /// The VK_KHR_surface vkGetPhysicalDeviceSurfaceFormatsKHR function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);    /// The VK_KHR_surface vkGetPhysicalDeviceSurfaceCapabilitiesKHR function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfacePresentModesKHR);    /// The VK_KHR_surface vkGetPhysicalDeviceSurfacePresentModesKHR function.

    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkCreateWin32SurfaceKHR);                      /// The VK_KHR_win32_surface vkCreateWin32SurfaceKHR function.
    
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkCreateDebugReportCallbackEXT);               /// The VK_EXT_debug_report vkCreateDebugReportCallbackEXT function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkDebugReportMessageEXT);                      /// The VK_EXT_debug_report vkDebugReportMessageEXT function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkDestroyDebugReportCallbackEXT);              /// The VK_EXT_debug_report vkDestroyDebugReportCallbackEXT function.
};

/// @summary Define the data describing the properties of all Vulkan-capable physical devices attached to the host, as well as any active display outputs.
struct OS_VULKAN_PHYSICAL_DEVICE_LIST
{
    size_t                            DeviceCount;                                   /// The number of Vulkan-capable physical devices in the system.
    VkPhysicalDevice                 *DeviceHandle;                                  /// An array where each element [Device] specifies the Vuklan handle of a physical device.
    VkPhysicalDeviceType             *DeviceType;                                    /// An array where each element [Device] specifies the device type (integrated GPU, discrete GPU, etc.) of a physical device.
    VkPhysicalDeviceFeatures         *DeviceFeatures;                                /// An array where each element [Device] describes the fine-grained features of a physical device.
    VkPhysicalDeviceProperties       *DeviceProperties;                              /// An array where each element [Device] specifies the basic capabilities of a physical device.
    VkPhysicalDeviceMemoryProperties *DeviceMemory;                                  /// An array where each element [Device] specifies the types of memory heaps available on a physical device.
    size_t                           *DeviceExtensionCount;                          /// An array where each element [Device] specifies the number of device-level extensions exposed by the runtime for a physical device.
    VkExtensionProperties           **DeviceExtensionProperties;                     /// An array where each element [Device][Extension] specifies the properties of a single device-level extension exposed by the runtime for a physical device.
    size_t                           *DeviceQueueFamilyCount;                        /// An array where each element [Device] specifies the number of queue families exposed by a physical device.
    VkQueueFamilyProperties         **DeviceQueueFamilyProperties;                   /// An array where each element [Device][QueueFamily] specifies the properties of a queue family exposed by a physical device.
    VkBool32                       ***DeviceQueueFamilyCanPresent;                   /// An array where each element [Device][QueueFamily][Display] specifies whether a queue family of a physical device can present to a given display.
    VkBool32                        **DeviceCanPresent;                              /// An array where each element [Device][Display] specifies whether at least one queue family of a physical device can present to a given display.
    VkSurfaceCapabilitiesKHR        **DeviceSurfaceCapabilities;                     /// An array where each element [Device][Display] specifies the basic capabilities of a physical device when presenting to a given display.
    size_t                          **DeviceSurfaceFormatCount;                      /// An array where each element [Device][Display] specifies the number of surface formats supported by a physical device for a given display.
    VkSurfaceFormatKHR             ***DeviceSurfaceFormats;                          /// An array where each element [Device][Display][SurfaceFormat] specifies the format and colorspace of a presentable surface format supported by a physical device for a given display.
    size_t                          **DevicePresentModeCount;                        /// An array where each element [Device][Display] specifies the number of presentation modes supported by a physical device for a given display.
    VkPresentModeKHR               ***DevicePresentModes;                            /// An array where each element [Device][Display][PresentMode] specifies a presentation mode supported by a physical device for a given display.

    size_t                            DisplayCount;                                  /// The number of display outputs on the host.
    HMONITOR                         *DisplayMonitor;                                /// An array where each element [Display] specifies the monitor handle of the display output.
    DISPLAY_DEVICE                   *DisplayDevice;                                 /// An array where each element [Display] specifies information about the display output device.
    DEVMODE                          *DisplayMode;                                   /// An array where each element [Display] specifies the display mode of a display output at the time of display enumeration.
};

/// @summary Define the device-level Vulkan runtime functions used to manage resources and submit work to the device.
struct OS_VULKAN_DEVICE_DISPATCH
{
    VkDevice                          DeviceHandle;                                  /// The Vulkan logical device handle used to interface with device-level API functions.
    VkPhysicalDevice                  PhysicalDeviceHandle;                          /// The handle of the Vulkan physical device that executes the application commands.

    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkGetDeviceProcAddr);                          /// The vkGetDeviceProcAddr function, copied from the OS_VULKAN_INSTANCE_DISPATCH object.
    
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkAllocateCommandBuffers);                     /// The vkAllocateCommandBuffers function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkAllocateDescriptorSets);                     /// The vkAllocateDescriptorSets function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkAllocateMemory);                             /// The vkAllocateMemory function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkBeginCommandBuffer);                         /// The vkBeginCommandBuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkBindBufferMemory);                           /// The vkBindBufferMemory function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkBindImageMemory);                            /// The vkBindImageMemory function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdBeginQuery);                              /// The vkCmdBeginQuery function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdBeginRenderPass);                         /// The vkCmdBeginRenderPass function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdBindDescriptorSets);                      /// The vkCmdBindDescriptorSets function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdBindIndexBuffer);                         /// The vkCmdBindIndexBuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdBindPipeline);                            /// The vkCmdBindPipeline function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdBindVertexBuffers);                       /// The vkCmdBindVertexBuffers function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdBlitImage);                               /// The vkCmdBlitImage function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdClearAttachments);                        /// The vkCmdClearAttachments function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdClearColorImage);                         /// The vkCmdClearColorImage function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdClearDepthStencilImage);                  /// The vkCmdClearDepthStencilImage function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdCopyBuffer);                              /// The vkCmdCopyBuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdCopyBufferToImage);                       /// The vkCmdCopyBufferToImage function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdCopyImage);                               /// The vkCmdCopyImage function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdCopyImageToBuffer);                       /// The vkCmdCopyImageToBuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdCopyQueryPoolResults);                    /// The vkCmdCopyQueryPoolResults function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdDispatch);                                /// The vkCmdDispatch function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdDispatchIndirect);                        /// The vkCmdDispatchIndirect function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdDraw);                                    /// The vkCmdDraw function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdDrawIndexed);                             /// The vkCmdDrawIndexed function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdDrawIndexedIndirect);                     /// The vkCmdDrawIndexedIndirect function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdDrawIndirect);                            /// The vkCmdDrawIndirect function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdEndQuery);                                /// The vkCmdEndQuery function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdEndRenderPass);                           /// The vkCmdEndRenderPass function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdExecuteCommands);                         /// The vkCmdExecuteCommands function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdFillBuffer);                              /// The vkCmdFillBuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdNextSubpass);                             /// The vkCmdNextSubpass function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdPipelineBarrier);                         /// The vkCmdPipelineBarrier function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdPushConstants);                           /// The vkCmdPushConstants function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdResetEvent);                              /// The vkCmdResetEvent function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdResetQueryPool);                          /// The vkCmdResetQueryPool function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdResolveImage);                            /// The vkCmdResolveImage function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdSetBlendConstants);                       /// The vkCmdSetBlendConstants function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdSetDepthBias);                            /// The vkCmdSetDepthBias function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdSetDepthBounds);                          /// The vkCmdSetDepthBounds function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdSetEvent);                                /// The vkCmdSetEvent function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdSetLineWidth);                            /// The vkCmdSetLineWidth function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdSetScissor);                              /// The vkCmdSetScissor function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdSetStencilCompareMask);                   /// The vkCmdSetStencilCompareMask function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdSetStencilReference);                     /// The vkCmdSetStencilReference function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdSetStencilWriteMask);                     /// The vkCmdSetStencilWriteMask function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdSetViewport);                             /// The vkCmdSetViewport function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdUpdateBuffer);                            /// The vkCmdUpdateBuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdWaitEvents);                              /// The vkCmdWaitEvents function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCmdWriteTimestamp);                          /// The vkCmdWriteTimestamp function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateBuffer);                               /// The vkCreateBuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateBufferView);                           /// The vkCreateBufferView function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateCommandPool);                          /// The vkCreateCommandPool function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateComputePipelines);                     /// The vkCreateComputePipelines function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateDescriptorPool);                       /// The vkCreateDescriptorPool function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateDescriptorSetLayout);                  /// The vkCreateDescriptorSetLayout function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateEvent);                                /// The vkCreateEvent function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateFence);                                /// The vkCreateFence function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateFramebuffer);                          /// The vkCreateFramebuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateGraphicsPipelines);                    /// The vkCreateGraphicsPipelines function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateImage);                                /// The vkCreateImage function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateImageView);                            /// The vkCreateImageView function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreatePipelineCache);                        /// The vkCreatePipelineCache function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreatePipelineLayout);                       /// The vkCreatePipelineLayout function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateQueryPool);                            /// The vkCreateQueryPool function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateRenderPass);                           /// The vkCreateRenderPass function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateSampler);                              /// The vkCreateSampler function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateSemaphore);                            /// The vkCreateSemaphore function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateShaderModule);                         /// The vkCreateShaderModule function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyBuffer);                              /// The vkDestroyBuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyBufferView);                          /// The vkDestroyBufferView function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyCommandPool);                         /// The vkDestroyCommandPool function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyDescriptorPool);                      /// The vkDestroyDescriptorPool function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyDescriptorSetLayout);                 /// The vkDestroyDescriptorSetLayout function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyDevice);                              /// The vkDestroyDevice function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyEvent);                               /// The vkDestroyEvent function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyFence);                               /// The vkDestroyFence function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyFramebuffer);                         /// The vkDestroyFramebuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyImage);                               /// The vkDestroyImage function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyImageView);                           /// The vkDestroyImageView function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyPipeline);                            /// The vkDestroyPipeline function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyPipelineCache);                       /// The vkDestroyPipelineCache function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyPipelineLayout);                      /// The vkDestroyPipelineLayout function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyQueryPool);                           /// The vkDestroyQueryPool function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyRenderPass);                          /// The vkDestroyRenderPass function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroySampler);                             /// The vkDestroySampler function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroySemaphore);                           /// The vkDestroySemaphore function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroyShaderModule);                        /// The vkDestroyShaderModule function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDeviceWaitIdle);                             /// The vkDeviceWaitIdle function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkEndCommandBuffer);                           /// The vkEndCommandBuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkFlushMappedMemoryRanges);                    /// The vkFlushMappedMemoryRanges function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkFreeCommandBuffers);                         /// The vkFreeCommandBuffers function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkFreeDescriptorSets);                         /// The vkFreeDescriptorSets function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkFreeMemory);                                 /// The vkFreeMemory function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetBufferMemoryRequirements);                /// The vkGetBufferMemoryRequirements function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetDeviceMemoryCommitment);                  /// The vkGetDevicememoryCommitment function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetDeviceQueue);                             /// The vkGetDeviceQueue function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetEventStatus);                             /// The vkGetEventStatus function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetFenceStatus);                             /// The vkGetFenceStatus function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetImageMemoryRequirements);                 /// The vkGetImageMemoryRequirements function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetImageSparseMemoryRequirements);           /// The vkGetImageSparseMemoryRequirements function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetImageSubresourceLayout);                  /// The vkGetImageSubresourceLayout function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetPipelineCacheData);                       /// The vkGetPipelineCacheData function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetQueryPoolResults);                        /// The vkGetQueryPoolResults function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetRenderAreaGranularity);                   /// The vkGetRenderAreaGranularity function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkInvalidateMappedMemoryRanges);               /// The vkInvalidateMappedMemoryRanges function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkMapMemory);                                  /// The vkMapMemory function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkMergePipelineCaches);                        /// The vkMergePipelineCaches function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkQueueBindSparse);                            /// The vkQueueBindSparse function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkQueueSubmit);                                /// The vkQueueSubmit function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkQueueWaitIdle);                              /// The vkQueueWaitIdle function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkResetCommandBuffer);                         /// The vkResetCommandBuffer function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkResetCommandPool);                           /// The vkResetCommandPool function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkResetDescriptorPool);                        /// The vkResetDescriptorPool function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkResetEvent);                                 /// The vkResetEvent function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkResetFences);                                /// The vkResetFences function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkSetEvent);                                   /// The vkSetEvent function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkUnmapMemory);                                /// The vkUnmapMemory function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkUpdateDescriptorSets);                       /// The vkUpdateDescriptorSets function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkWaitForFences);                              /// The vkWaitForFences function.

    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkAcquireNextImageKHR);                        /// The VK_KHR_swapchain vkAcquireNextImageKHR function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateSharedSwapchainsKHR);                  /// The VK_KHR_swapchain vkCreateSharedSwapchainsKHR function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkCreateSwapchainKHR);                         /// The VK_KHR_swapchain vkCreateSwapchainKHR function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkDestroySwapchainKHR);                        /// The VK_KHR_swapchain vkDestroySwapchainKHR function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkGetSwapchainImagesKHR);                      /// The VK_KHR_swapchain vkGetSwapchainImagesKHR function.
    OS_LAYER_VULKAN_DEVICE_FUNCTION  (vkQueuePresentKHR);                            /// The VK_KHR_swapchain vkQueuePresentKHR function.
};

/// @summary Defines the data associated with an active sound output device on the host.
struct OS_AUDIO_OUTPUT_DEVICE
{   static size_t const               MAX_DEVICE_ID = 128;                           /// The maximum supported size of a device ID string, including the zero-terminator, in characters.
    IMMDevice                        *Device;                                        /// The multimedia device interface to the output device.
    IAudioClient                     *AudioClient;                                   /// The WASAPI audio client associated with the output device.
    IAudioRenderClient               *RenderClient;                                  /// The WASAPI interface used to write audio data to the device.
    IAudioClock                      *AudioClock;                                    /// The WASAPI interface used to read the current read position within the output buffer.
    uint32_t                          RequestedBufferSize;                           /// The requested buffer size, in samples.
    uint32_t                          ActualBufferSize;                              /// The buffer size reported by the device, in samples.
    uint32_t                          SamplesPerSecond;                              /// The requested sample rate, in samples-per-second.
    WAVEFORMATEXTENSIBLE              AudioFormat;                                   /// The format of the audio data expected by the device (16-bit stereo PCM.)
    WCHAR                             DeviceId[MAX_DEVICE_ID];                       /// A copy of the device ID string, zero-terminated. 
};

/// @summary Defines the data associated with an active sound capture device on the host.
struct OS_AUDIO_CAPTURE_DEVICE
{
    IMMDevice                        *Device;                                        /// The multimedia device interface to the capture device.
    IAudioClient                     *AudioClient;                                   /// The WASAPI audio client associated with the capture device.
    IAudioCaptureClient              *CaptureClient;                                 /// The WASAPI interface used to read audio data from the device.
    IAudioClock                      *AudioClock;                                    /// The WASAPI interface used to read the current write position within the capture buffer.
};

/// @summary Defines the data associated with the host audio system used for capturing and playing back sound data.
struct OS_AUDIO_SYSTEM
{
    IMMDeviceEnumerator              *DeviceEnumerator;                              /// The system audio device enumerator interface.
    WCHAR                            *DefaultOutputDeviceId;                         /// The unique identifier of the default sound output device.
    WCHAR                            *DefaultCaptureDeviceId;                        /// The unique identifier of the default sound capture device.
    bool                              AudioOutputEnabled;                            /// true if audio output is enabled.
    bool                              AudioCaptureEnabled;                           /// true if audio capture is enabled.
    OS_AUDIO_OUTPUT_DEVICE            ActiveOutputDevice;                            /// Data associated with the active sound output device.
    OS_AUDIO_CAPTURE_DEVICE           ActiveCaptureDevice;                           /// Data associated with the active sound capture device.
};

/// @summary Defines the data associated with the list of audio output and capture devices on the host.
struct OS_AUDIO_DEVICE_LIST
{
    size_t                            OutputDeviceCount;                             /// The number of audio output devices on the host, not including disabled output devices.
    WCHAR                           **OutputDeviceId;                                /// An array where each element [Device] specifies a zero-terminated string used to uniquely identify an enabled audio output device.
    WCHAR                           **OutputDeviceName;                              /// An array where each element [Device] specifies a zero-terminated string representing the friendly name of an enabled audio output device.

    size_t                            CaptureDeviceCount;                            /// The number of audio capture devices on the host, not including disabled capture devices.
    WCHAR                           **CaptureDeviceId;                               /// An array where each element [Device] specifies a zero-terminated string used to uniquely identify an enabled audio capture device.
    WCHAR                           **CaptureDeviceName;                             /// An array where each element [Device] specifies a zero-terminated string representing the friendly name of an enabled audio capture device.
};

/// @summary Define the errors that can be returned when defining a task.
enum OS_TASK_POOL_ERROR               : int32_t
{
    OS_TASK_POOL_ERROR_NONE           = 0,           /// The task was defined successfully.
    OS_TASK_POOL_ERROR_TASK_LIMIT     = 1,           /// The task could not be defined because the pool has no available slots.
    OS_TASK_POOL_ERROR_DATA_LIMIT     = 2,           /// The task could not be defined because the per-task parameter data exceeds the maximum size.
    OS_TASK_POOL_ERROR_PERMIT_LIMIT   = 3,           /// The task could not be defined because one of the dependent tasks exceeds the maximum number of permits.
    OS_TASK_POOL_ERROR_INVALID_THREAD = 4,           /// The task could not be defined because the thread calling DefineTask does not match the thread that allocated the task pool.
    OS_TASK_POOL_ERROR_INVALID_PARENT = 5,           /// The task could not be defined because the parent task ID is invalid.
    OS_TASK_POOL_ERROR_INVALID_DATA   = 6,           /// The task could not be defined because no per-task parameter data was supplied.
};

/// @summary Define the valid values for a task data slot marker.
enum OS_TASK_SLOT_STATUS             : uint8_t
{
    OS_TASK_SLOT_STATUS_FREE         = 0,            /// The task slot is currently unused.
    OS_TASK_SLOT_STATUS_USED         = 1,            /// The task slot is currently used by an active task.
};

/// @summary Define constants representing the values of the task ID valid bit.
enum OS_TASK_ID_VALIDITY             : uint32_t
{
    OS_TASK_ID_INVALID               = 0,            /// The task ID is not valid.
    OS_TASK_ID_VALID                 = 1,            /// The task ID is valid.
};

/// @summary Define constants representing the values of the task ID type bit.
enum OS_TASK_ID_TYPE                 : uint32_t
{
    OS_TASK_ID_TYPE_EXTERNAL         = 0,            /// The task ID represents an external task completed by an external event.
    OS_TASK_ID_TYPE_INTERNAL         = 1,            /// The task ID represents an internal task completed by a worker thread.
};

/// @summary Define constants for specifying worker thread stack sizes.
enum OS_WORKER_THREAD_STACK_SIZE     : size_t
{
    OS_WORKER_THREAD_STACK_DEFAULT   = 0,            /// Use the default stack size for each worker thread.
};

/// @summary Define the set of return codes expected from the OS_WORKER_INIT callback.
enum OS_WORKER_THREAD_INIT_RESULT    : int
{
    OS_WORKER_THREAD_INIT_SUCCESS    = 0,            /// The worker thread initialized successfully.
    OS_WORKER_THREAD_INIT_FAILED     =-1,            /// The worker thread failed to initialize and the thread should be terminated.
};

/// @summary Define the set of reasons for waking a sleeping worker thread and invoking the worker callback.
enum OS_WORKER_THREAD_WAKE_REASON    : int
{
    OS_WORKER_THREAD_WAKE_FOR_EXIT   = 0,            /// The thread was woken because the thread pool is being terminated.
    OS_WORKER_THREAD_WAKE_FOR_SIGNAL = 1,            /// The thread was woken because of a general signal.
    OS_WORKER_THREAD_WAKE_FOR_RUN    = 2,            /// The thread was woken because an explicit work wakeup signal was sent.
    OS_WORKER_THREAD_WAKE_FOR_ERROR  = 3,            /// The thread was woken because of an error in GetQueuedCompletionStatus.
};

/// @summary Define the supported types of asynchronous I/O requests.
enum OS_IO_REQUEST_TYPE              : int
{
    OS_IO_REQUEST_NOOP               = 0,            /// Ignore the operation; pass through all data to the completion callback.
    OS_IO_REQUEST_OPEN_FILE          = 1,            /// Asynchronously open a file, but do not issue any other I/O operations.
    OS_IO_REQUEST_READ_FILE          = 2,            /// Issue an explicit asynchronous read request.
    OS_IO_REQUEST_WRITE_FILE         = 3,            /// Issue an explicit asynchronous write request.
    OS_IO_REQUEST_FLUSH_FILE         = 4,            /// Issue an explicit asynchronous flush request.
    OS_IO_REQUEST_CLOSE_FILE         = 5,            /// Asynchronously close a file.
};

/// @summary Define the states that an I/O request can be in.
enum OS_IO_REQUEST_STATE             : int
{
    OS_IO_REQUEST_STATE_CHAINED      = 0,            /// The I/O request has been submitted as a chained request, which executes immediately and is not queued.
    OS_IO_REQUEST_STATE_SUBMITTED    = 1,            /// The I/O request has been submitted, but not yet launched.
    OS_IO_REQUEST_STATE_LAUNCHED     = 2,            /// The I/O request has been picked up by an I/O thread and has begun executing.
    OS_IO_REQUEST_STATE_COMPLETED    = 3,            /// The I/O request has been completed.
};

/// @summary Defines identifiers for special well-known directories.
enum OS_KNOWN_PATH                   : int
{
    OS_KNOWN_PATH_EXECUTABLE         = 0,            /// The absolute path to the current executable, without filename.
    OS_KNOWN_PATH_USER_HOME          = 1,            /// The absolute path to the user's home directory.
    OS_KNOWN_PATH_USER_DESKTOP       = 2,            /// The absolute path to the user's desktop directory.
    OS_KNOWN_PATH_USER_DOCUMENTS     = 3,            /// The absolute path to the user's documents directory.
    OS_KNOWN_PATH_USER_DOWNLOADS     = 4,            /// The absolute path to the user's downloads directory.
    OS_KNOWN_PATH_USER_MUSIC         = 5,            /// The absolute path to the user's music directory.
    OS_KNOWN_PATH_USER_PICTURES      = 6,            /// The absolute path to the user's pictures directory.
    OS_KNOWN_PATH_USER_SAVE_GAMES    = 7,            /// The absolute path to the user's save games directory.
    OS_KNOWN_PATH_USER_VIDEOS        = 8,            /// The absolute path to the user's videos directory.
    OS_KNOWN_PATH_USER_PREFERENCES   = 9,            /// The absolute path to the user's preferences directory.
    OS_KNOWN_PATH_PUBLIC_DOCUMENTS   = 10,           /// The absolute path to the public documents directory.
    OS_KNOWN_PATH_PUBLIC_DOWNLOADS   = 11,           /// The absolute path to the public downloads directory.
    OS_KNOWN_PATH_PUBLIC_MUSIC       = 12,           /// The absolute path to the public music directory.
    OS_KNOWN_PATH_PUBLIC_PICTURES    = 13,           /// The absolute path to the public pictures directory.
    OS_KNOWN_PATH_PUBLIC_VIDEOS      = 14,           /// The absolute path to the public videos directory.
    OS_KNOWN_PATH_SYSTEM_FONTS       = 15,           /// The absolute path to the system fonts directory.
};

/// @summary Define the possible result codes returned by OsCreateVulkanLoader.
enum OS_VULKAN_LOADER_RESULT         : int
{
    OS_VULKAN_LOADER_RESULT_SUCCESS  = 0,            /// The Vulkan loader was successfully created; the host supports Vulkan.
    OS_VULKAN_LOADER_RESULT_NOVULKAN =-1,            /// The host system does not expose any Vulkan ICDs.
    OS_VULKAN_LOADER_RESULT_NOMEMORY =-2,            /// The supplied memory arena does not have sufficient space.
    OS_VULKAN_LOADER_RESULT_NOENTRY  =-3,            /// One or more required Vulkan API entry points are missing.
    OS_VULKAN_LOADER_RESULT_VKERROR  =-4,            /// A Vulkan API call returned an error.
};

/// @summary Define flags that can be bitwise OR'd to specify the attributes of a host memory allocation.
enum OS_HOST_MEMORY_ALLOCATION_FLAGS  : uint32_t
{
    OS_HOST_MEMORY_ALLOCATION_FLAGS_NONE         = (0 << 0), /// No flags are specified. The allocation allocation will allow reading, writing and end with a guard page.
    OS_HOST_MEMORY_ALLOCATION_FLAG_READ          = (1 << 0), /// The memory is readable by the host.
    OS_HOST_MEMORY_ALLOCATION_FLAG_WRITE         = (1 << 1), /// The memory is writable by the host.
    OS_HOST_MEMORY_ALLOCATION_FLAG_EXECUTE       = (1 << 2), /// The memory is will contain dynamically-generated executable code.
    OS_HOST_MEMORY_ALLOCATION_FLAG_NO_GUARD_PAGE = (1 << 3), /// The memory allocation will not end with a trailing guard page.
    OS_HOST_MEMORY_ALLOCATION_FLAGS_READWRITE    = OS_HOST_MEMORY_ALLOCATION_FLAG_READ | OS_HOST_MEMORY_ALLOCATION_FLAG_WRITE,
};

/// @summary Define the valid flags that can be specified to define the usage for an OS_TASK_POOL. Valid combinations are:
/// OS_TASK_POOL_USAGE_FLAG_DEFINE | OS_TASK_USAGE_FLAG_PUBLISH: The thread defines tasks to be stolen and executed on worker threads.
/// OS_TASK_POOL_USAGE_FLAG_DEFINE | OS_TASK_USAGE_FLAG_EXECUTE: The thread defines tasks and can also execute tasks manually.
/// OS_TASK_POOL_USAGE_FLAG_DEFINE | OS_TASK_USAGE_FLAG_EXECUTE | OS_TASK_USAGE_FLAG_WORKER: The thread defines tasks and can also execute them in the background.
enum OS_TASK_POOL_USAGE_FLAGS        : uint32_t
{
    OS_TASK_POOL_USAGE_FLAGS_NONE    = (0 << 0),     /// No flags are specified for the task pool. This is invalid.
    OS_TASK_POOL_USAGE_FLAG_DEFINE   = (1 << 0),     /// The thread that owns the task pool can define tasks.
    OS_TASK_POOL_USAGE_FLAG_EXECUTE  = (1 << 1),     /// The thread that owns the task pool can execute tasks.
    OS_TASK_POOL_USAGE_FLAG_PUBLISH  = (1 << 2),     /// The thread that owns the task pool can publish task notifications.
    OS_TASK_POOL_USAGE_FLAG_WORKER   = (1 << 3),     /// The thread that owns the task pool is a worker thread.
};

/// @summary Define the valid flags that can be set on the OS_PATH_PARTS::PathFlags field.
enum OS_PATH_FLAGS                   : uint32_t
{
    OS_PATH_FLAGS_INVALID            = (0 << 0),     /// No flags are specified for the path, or the path cannot be parsed.
    OS_PATH_FLAG_ABSOLUTE            = (1 << 0),     /// The path string specifies an absolute path.
    OS_PATH_FLAG_RELATIVE            = (1 << 1),     /// The path string specifies a relative path.
    OS_PATH_FLAG_UNC                 = (1 << 2),     /// The path string specifies a path in UNC format.
    OS_PATH_FLAG_DEVICE              = (1 << 3),     /// The path string specifies a device path.
    OS_PATH_FLAG_LONG                = (1 << 4),     /// The path string specifies a long path, with a maximum length of 32767 characters.
    OS_PATH_FLAG_ROOT                = (1 << 5),     /// The path string has a root component.
    OS_PATH_FLAG_PATH                = (1 << 6),     /// The path string has a directory path component.
    OS_PATH_FLAG_FILENAME            = (1 << 7),     /// The path string has a filename component.
    OS_PATH_FLAG_EXTENSION           = (1 << 8),     /// The path string has a file extension component.
};

/// @summary Define various allocation attributes of a file region.
enum OS_FILE_DATA_FLAGS              : uint32_t
{
    OS_FILE_DATA_FLAGS_NONE          = (0 << 0),      /// The OS_FILE_DATA is invalid.
    OS_FILE_DATA_FLAG_COMMITTED      = (1 << 0),      /// The OS_FILE_DATA buffer is an explicitly allocated region of memory.
    OS_FILE_DATA_FLAG_MAPPED_REGION  = (1 << 1),      /// The OS_FILE_DATA represents a mapped region of a file.
};

/// @summary Define flags used to optimize asynchronous I/O operations. The usage hints are specified when the file is opened.
enum OS_IO_HINT_FLAGS                : uint32_t
{
    OS_IO_HINT_FLAGS_NONE            = (0 << 0),      /// No I/O hints are specified, use the default behavior appropriate for the I/O request type.
    OS_IO_HINT_FLAG_READ             = (1 << 0),      /// Read operations will be issued against the file.
    OS_IO_HINT_FLAG_WRITE            = (1 << 1),      /// Write operations will be issues against the file.
    OS_IO_HINT_FLAG_OVERWRITE        = (1 << 2),      /// The existing file contents should be discarded.
    OS_IO_HINT_FLAG_PREALLOCATE      = (1 << 3),      /// Preallocate the file to the size specified in the OS_IO_REQUEST::DataAmount field.
    OS_IO_HINT_FLAG_SEQUENTIAL       = (1 << 4),      /// Optimize for sequential access when performing cached/buffered I/O.
    OS_IO_HINT_FLAG_UNCACHED         = (1 << 5),      /// Indicate that the I/O should bypass the OS page cache, and that the source or destination buffer meets sector alignment requirements (valid for OPEN, LOAD and SAVE).
    OS_IO_HINT_FLAG_WRITE_THROUGH    = (1 << 6),      /// Indicate that writes should be immediately flushed to disk.
    OS_IO_HINT_FLAG_TEMPORARY        = (1 << 7),      /// Indicate that the file is temporary, and will be deleted when the file handle is closed.
};

/// @summary Define flags indicating how to interpret OS_POINTER_STATE::Relative.
enum OS_POINTER_FLAGS                : uint32_t
{
    OS_POINTER_FLAGS_NONE            = (0 << 0),     /// No special flags are specified with the pointer data.
    OS_POINTER_FLAGS_ABSOLUTE        = (1 << 0),     /// Only absolute position was specified.
};

/// @summary Define the possible values for membership in a Raw Input device set.
enum OS_INPUT_DEVICE_SET_MEMBERSHIP  : uint32_t
{
    OS_INPUT_DEVICE_MEMBERSHIP_NONE  = (0 << 0),     /// The Raw Input device is not in either the current or previous snapshot.
    OS_INPUT_DEVICE_MEMBERSHIP_PREV  = (1 << 0),     /// The Raw Input device is in the previous state snapshot.
    OS_INPUT_DEVICE_MEMBERSHIP_CURR  = (1 << 1),     /// The Raw Input device is in the current state snapshot.
    OS_INPUT_DEVICE_MEMBERSHIP_BOTH  = OS_INPUT_DEVICE_MEMBERSHIP_PREV | OS_INPUT_DEVICE_MEMBERSHIP_CURR
};

/// @summary Define a macro for easy static initialization of keyboard state data.
#ifndef OS_KEYBOARD_STATE_STATIC_INIT
#define OS_KEYBOARD_STATE_STATIC_INIT                                          \
    {                                                                          \
        { 0, 0, 0, 0, 0, 0, 0, 0 } /* KeyState */                              \
    }
#endif

/// @summary Define a macro for easy static initialization of gamepad state data.
#ifndef OS_GAMEPAD_STATE_STATIC_INIT
#define OS_GAMEPAD_STATE_STATIC_INIT                                           \
    {                                                                          \
        0,            /* LTrigger */                                           \
        0,            /* RTrigger */                                           \
        0,            /* Buttons */                                            \
      { 0, 0, 0, 0 }, /* LStick[X,Y,M,N] */                                    \
      { 0, 0, 0, 0 }  /* RStick[X,Y,M,N] */                                    \
    }
#endif

// @summary Define a macro for easy static initialization of pointer state data.
#ifndef OS_POINTER_STATE_STATIC_INIT
#define OS_POINTER_STATE_STATIC_INIT                                           \
    {                                                                          \
      { 0, 0 }   , /* Pointer[X,Y] */                                          \
      { 0, 0, 0 }, /* Relative[X,Y,Z] */                                       \
        0          /* Buttons */                                               \
    }
#endif

/// @summary Define a macro for easy static initialization of a keyboard device list.
#ifndef OS_KEYBOARD_LIST_STATIC_INIT
#define OS_KEYBOARD_LIST_STATIC_INIT                                           \
    {                                                                          \
        0, /* DeviceCount */                                                   \
      { OS_INPUT_DEVICE_HANDLE_NONE,                                           \
        OS_INPUT_DEVICE_HANDLE_NONE,                                           \
        OS_INPUT_DEVICE_HANDLE_NONE,                                           \
        OS_INPUT_DEVICE_HANDLE_NONE                                            \
      },   /* DeviceHandle */                                                  \
      { OS_KEYBOARD_STATE_STATIC_INIT,                                         \
        OS_KEYBOARD_STATE_STATIC_INIT,                                         \
        OS_KEYBOARD_STATE_STATIC_INIT,                                         \
        OS_KEYBOARD_STATE_STATIC_INIT                                          \
      }    /* DeviceState */                                                   \
    }
#endif

/// @summary Define a macro for easy static initialzation of a gamepad device list.
#ifndef OS_GAMEPAD_LIST_STATIC_INIT
#define OS_GAMEPAD_LIST_STATIC_INIT                                            \
    {                                                                          \
        0, /* DeviceCount */                                                   \
      { OS_INPUT_DEVICE_HANDLE_NONE,                                           \
        OS_INPUT_DEVICE_HANDLE_NONE,                                           \
        OS_INPUT_DEVICE_HANDLE_NONE,                                           \
        OS_INPUT_DEVICE_HANDLE_NONE                                            \
      },   /* DeviceHandle */                                                  \
      { OS_GAMEPAD_STATE_STATIC_INIT,                                          \
        OS_GAMEPAD_STATE_STATIC_INIT,                                          \
        OS_GAMEPAD_STATE_STATIC_INIT,                                          \
        OS_GAMEPAD_STATE_STATIC_INIT                                           \
      }    /* DeviceState */                                                   \
    }
#endif

/// @summary Define a macro for easy static initialization of a pointing device list.
#ifndef OS_POINTER_LIST_STATIC_INIT
#define OS_POINTER_LIST_STATIC_INIT                                            \
    {                                                                          \
        0, /* DeviceCount */                                                   \
      { OS_INPUT_DEVICE_HANDLE_NONE,                                           \
        OS_INPUT_DEVICE_HANDLE_NONE,                                           \
        OS_INPUT_DEVICE_HANDLE_NONE,                                           \
        OS_INPUT_DEVICE_HANDLE_NONE                                            \
      },   /* DeviceHandle */                                                  \
      { OS_POINTER_STATE_STATIC_INIT,                                          \
        OS_POINTER_STATE_STATIC_INIT,                                          \
        OS_POINTER_STATE_STATIC_INIT,                                          \
        OS_POINTER_STATE_STATIC_INIT                                           \
      }    /* DeviceState */                                                   \
    }
#endif

/// @summary Define a macro for easy initialization of a device set to empty.
#ifndef OS_INPUT_DEVICE_SET_STATIC_INIT
#define OS_INPUT_DEVICE_SET_STATIC_INIT                                        \
    {                                                                          \
        0, /* DeviceCount */                                                   \
        { OS_INPUT_DEVICE_HANDLE_NONE,                                         \
          OS_INPUT_DEVICE_HANDLE_NONE,                                         \
          OS_INPUT_DEVICE_HANDLE_NONE,                                         \
          OS_INPUT_DEVICE_HANDLE_NONE,                                         \
          OS_INPUT_DEVICE_HANDLE_NONE,                                         \
          OS_INPUT_DEVICE_HANDLE_NONE,                                         \
          OS_INPUT_DEVICE_HANDLE_NONE,                                         \
          OS_INPUT_DEVICE_HANDLE_NONE                                          \
        }, /* DeviceIds */                                                     \
        { OS_INPUT_DEVICE_MEMBERSHIP_NONE,                                     \
          OS_INPUT_DEVICE_MEMBERSHIP_NONE,                                     \
          OS_INPUT_DEVICE_MEMBERSHIP_NONE,                                     \
          OS_INPUT_DEVICE_MEMBERSHIP_NONE,                                     \
          OS_INPUT_DEVICE_MEMBERSHIP_NONE,                                     \
          OS_INPUT_DEVICE_MEMBERSHIP_NONE,                                     \
          OS_INPUT_DEVICE_MEMBERSHIP_NONE,                                     \
          OS_INPUT_DEVICE_MEMBERSHIP_NONE                                      \
        }, /* Membership */                                                    \
        {                                                                      \
          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF                       \
        }, /* PrevIndex */                                                     \
        {                                                                      \
          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF                       \
        }  /* CurrIndex */                                                     \
    }
#endif

/// @summary The set of functions resolved at runtime. Do not link to the corresponding input library; the functions are defined in this module.
OS_LAYER_DECLARE_RUNTIME_FUNCTION(void , WINAPI, XInputEnable               , BOOL);                                      // XInput1_4.dll
OS_LAYER_DECLARE_RUNTIME_FUNCTION(DWORD, WINAPI, XInputGetState             , DWORD, XINPUT_STATE*);                      // XInput1_4.dll
OS_LAYER_DECLARE_RUNTIME_FUNCTION(DWORD, WINAPI, XInputSetState             , DWORD, XINPUT_VIBRATION*);                  // XInput1_4.dll
OS_LAYER_DECLARE_RUNTIME_FUNCTION(DWORD, WINAPI, XInputGetCapabilities      , DWORD, DWORD, XINPUT_CAPABILITIES*);        // XInput1_4.dll
//OS_LAYER_DECLARE_RUNTIME_FUNCTION(DWORD, WINAPI, XInputGetBatteryInformation, DWORD, BYTE , XINPUT_BATTERY_INFORMATION*); // XInput1_4.dll

/*///////////////
//   Globals   //
///////////////*/
/// @summary Function pointers for the set of functions resolved at runtime.
OS_LAYER_DEFINE_RUNTIME_FUNCTION(XInputEnable);
OS_LAYER_DEFINE_RUNTIME_FUNCTION(XInputGetState);
OS_LAYER_DEFINE_RUNTIME_FUNCTION(XInputSetState);
OS_LAYER_DEFINE_RUNTIME_FUNCTION(XInputGetCapabilities);
//OS_LAYER_DEFINE_RUNTIME_FUNCTION(XInputGetBatteryInformation);

/// @summary The module load address of the XInput DLL.
global_variable HMODULE         XInputDll = NULL;

/// @summary OVERLAPPED_ENTRY::lpCompletionKey is set to OS_IO_COMPLETION_KEY_SHUTDOWN to terminate the asynchronous I/O thread loop.
global_variable ULONG_PTR const OS_COMPLETION_KEY_SHUTDOWN = ~ULONG_PTR(0);

/// @summary The GUID of the Win32 OS Layer task profiler provider {349CE0E9-6DF5-4C25-AC5B-C84F529BC0CE}.
global_variable GUID      const TaskProfilerGUID = { 0x349ce0e9, 0x6df5, 0x4c25, { 0xac, 0x5b, 0xc8, 0x4f, 0x52, 0x9b, 0xc0, 0xce } };

/*////////////////////////////
//   Forward Declarations   //
////////////////////////////*/
public_function void                       OsZeroMemory(void *dst, size_t len);
public_function void                       OsSecureZeroMemory(void *dst, size_t len);
public_function void                       OsCopyMemory(void * __restrict dst, void const * __restrict src, size_t len);
public_function void                       OsMoveMemory(void *dst, void const *src, size_t len);
public_function void                       OsFillMemory(void *dst, size_t len, uint8_t val);
public_function size_t                     OsAlignUp(size_t size, size_t pow2);
public_function OS_MEMORY_RANGE            OsInitHostMemoryRange(void *addr, size_t size);
public_function OS_MEMORY_RANGE            OsInitHostMemoryRange(OS_HOST_MEMORY_ALLOCATION *memory);
public_function int                        OsCreateHostMemoryPool(OS_HOST_MEMORY_POOL *pool, OS_HOST_MEMORY_POOL_INIT *init);
public_function void                       OsDeleteHostMemoryPool(OS_HOST_MEMORY_POOL *pool);
public_function OS_HOST_MEMORY_ALLOCATION* OsHostMemoryPoolAllocate(OS_HOST_MEMORY_POOL *pool, size_t reserve_size, size_t commit_size, uint32_t alloc_flags);
public_function void                       OsHostMemoryPoolRelease(OS_HOST_MEMORY_POOL *pool, OS_HOST_MEMORY_ALLOCATION *alloc);
public_function void                       OsHostMemoryPoolReset(OS_HOST_MEMORY_POOL *pool);
public_function int                        OsHostMemoryReserveAndCommit(OS_HOST_MEMORY_ALLOCATION *alloc, size_t reserve_size, size_t commit_size, uint32_t alloc_flags);
public_function int                        OsHostMemoryIncreaseCommitment(OS_HOST_MEMORY_ALLOCATION *alloc, size_t commit_size);
public_function void                       OsHostMemoryFlush(OS_HOST_MEMORY_ALLOCATION *alloc);
public_function void                       OsHostMemoryRelease(OS_HOST_MEMORY_ALLOCATION *alloc);
public_function int                        OsCreateArenaAllocator(OS_ARENA_ALLOCATOR *alloc, size_t size_in_bytes);
public_function void                       OsDeleteArenaAllocator(OS_ARENA_ALLOCATOR *alloc);
public_function bool                       OsArenaAllocatorCanSatisfyAllocation(OS_ARENA_ALLOCATOR *alloc, size_t size, size_t alignment);
public_function bool                       OsArenaAllocate(OS_ARENA_ALLOCATOR *alloc, size_t size, size_t alignment, OS_MEMORY_RANGE &range);
public_function os_arena_marker_t          OsArenaMark(OS_ARENA_ALLOCATOR *alloc);
public_function void                       OsArenaResetToMarker(OS_ARENA_ALLOCATOR *alloc, os_arena_marker_t marker);
public_function void                       OsArenaReset(OS_ARENA_ALLOCATOR *alloc);
public_function int                        OsCreateBuddyAllocator(OS_BUDDY_ALLOCATOR *alloc, OS_BUDDY_ALLOCATOR_INIT *init);
public_function void                       OsDeleteBuddyAllocator(OS_BUDDY_ALLOCATOR *alloc);
public_function bool                       OsBuddyAllocate(OS_BUDDY_ALLOCATOR *alloc, size_t size, size_t alignment, OS_MEMORY_RANGE &range);
public_function bool                       OsBuddyReallocate(OS_BUDDY_ALLOCATOR *alloc, OS_MEMORY_RANGE existing, size_t required_size, OS_MEMORY_RANGE &range);
public_function size_t                     OsBuddyBlockSize(OS_BUDDY_ALLOCATOR *alloc, size_t block_offset);
public_function void                       OsBuddyFree(OS_BUDDY_ALLOCATOR *alloc, OS_MEMORY_RANGE range);
public_function void                       OsBuddyReset(OS_BUDDY_ALLOCATOR *alloc);
public_function int                        OsCreateHostMemoryArena(OS_HOST_MEMORY_ARENA *arena, OS_MEMORY_RANGE host_memory);
public_function void                       OsDeleteHostMemoryArena(OS_HOST_MEMORY_ARENA *arena);
public_function bool                       OsHostMemoryArenaCanSatisfyAllocation(OS_HOST_MEMORY_ARENA *arena, size_t size, size_t alignment);
public_function void*                      OsHostMemoryArenaAllocate(OS_HOST_MEMORY_ARENA *arena, size_t size, size_t alignment);
public_function os_arena_marker_t          OsHostMemoryArenaMark(OS_HOST_MEMORY_ARENA *arena);
public_function void                       OsHostMemoryArenaResetToMarker(OS_HOST_MEMORY_ARENA *arena, os_arena_marker_t arena_marker);
public_function void                       OsHostMemoryArenaReset(OS_HOST_MEMORY_ARENA *arena);
public_function int                        OsCreateHostMemoryAllocator(OS_HOST_MEMORY_ALLOCATOR *alloc, OS_MEMORY_RANGE host_memory);
public_function void                       OsDeleteHostMemoryAllocator(OS_HOST_MEMORY_ALLOCATOR *alloc);
public_function void                       OsHostMemoryAllocatorReset(OS_HOST_MEMORY_ALLOCATOR *alloc);
public_function void*                      OsHostMemoryAllocate(OS_HOST_MEMORY_ALLOCATOR *alloc, size_t size, size_t alignment);
public_function void*                      OsHostMemoryAllocate(OS_HOST_MEMORY_ALLOCATOR *alloc, size_t size, size_t alignment, OS_MEMORY_RANGE &info);
public_function void*                      OsHostMemoryReallocate(OS_HOST_MEMORY_ALLOCATOR *alloc, void* existing, size_t new_size, size_t alignment);
public_function void*                      OsHostMemoryReallocate(OS_HOST_MEMORY_ALLOCATOR *alloc, void* existing, size_t new_size, size_t alignment, OS_MEMORY_RANGE &info);
public_function void                       OsHostMemoryFree(OS_HOST_MEMORY_ALLOCATOR *alloc, void *ptr);
public_function void                       OsHostMemoryFree(OS_HOST_MEMORY_ALLOCATOR *alloc, OS_MEMORY_RANGE info);

public_function uint64_t                   OsTimestampInTicks(void);
public_function uint64_t                   OsTimestampInNanoseconds(void);
public_function uint64_t                   OsNanosecondSliceOfSecond(uint64_t fraction);
public_function uint64_t                   OsElapsedNanoseconds(uint64_t start_ticks, uint64_t end_ticks);
public_function uint64_t                   OsMillisecondsToNanoseconds(uint32_t milliseconds);
public_function uint32_t                   OsNanosecondsToWholeMilliseconds(uint64_t nanoseconds);
public_function bool                       OsQueryHostCpuLayout(OS_CPU_INFO *cpu_info, OS_MEMORY_RANGE scratch_mem);

public_function uint32_t                   OsThreadId(void);
public_function os_task_id_t               OsMakeTaskId(uint32_t type, uint32_t pool, uint32_t index, uint32_t valid);
public_function bool                       OsIsValidTask(os_task_id_t task_id);
public_function bool                       OsIsExternalTask(os_task_id_t task_id);
public_function bool                       OsIsInternalTask(os_task_id_t task_id);
public_function unsigned int __cdecl       OsTaskSchedulerThreadMain(void *argp);
public_function int                        OsCreateTaskScheduler(OS_TASK_SCHEDULER *scheduler, OS_TASK_SCHEDULER_INIT *init, char const *name);
public_function void                       OsDestroyTaskScheduler(OS_TASK_SCHEDULER *scheduler);
public_function int                        OsAllocateTaskPool(OS_TASK_ENVIRONMENT *taskenv, OS_TASK_SCHEDULER *scheduler, uint32_t pool_type, uint32_t thread_id);
public_function void                       OsReturnTaskPool(OS_TASK_ENVIRONMENT *taskenv);
public_function int                        OsGetTaskPoolError(OS_TASK_ENVIRONMENT *taskenv);
public_function void                       OsSetTaskPoolError(OS_TASK_ENVIRONMENT *taskenv, int last_error);
public_function void                       OsPublishTasks(OS_TASK_ENVIRONMENT *taskenv, size_t task_count);
public_function size_t                     OsCompleteTask(OS_TASK_ENVIRONMENT *taskenv, os_task_id_t task_id);
public_function size_t                     OsFinishTaskDefinition(OS_TASK_ENVIRONMENT *taskenv, os_task_id_t task_id);
public_function os_task_id_t               OsDefineTask(OS_TASK_ENVIRONMENT *taskenv, uint32_t const task_type, OS_TASK_ENTRYPOINT task_main, void const *task_args, size_t const args_size, os_task_id_t const *dependency_list, size_t const dependency_count);
public_function os_task_id_t               OsDefineChildTask(OS_TASK_ENVIRONMENT *taskenv, uint32_t const task_type, OS_TASK_ENTRYPOINT task_main, void const *task_args, size_t const args_size, os_task_id_t const parent_id, os_task_id_t const *dependency_list, size_t const dependency_count);
public_function void                       OsWaitForTask(OS_TASK_ENVIRONMENT *taskenv, os_task_id_t wait_task);
public_function int                        OsAllocateTaskFence(OS_TASK_FENCE *fence);
public_function void                       OsDestroyTaskFence(OS_TASK_FENCE *fence);
public_function void                       OsResetTaskFence(OS_TASK_FENCE *fence);
public_function bool                       OsWaitTaskFence(OS_TASK_FENCE *fence, uint64_t timeout_ns);
public_function os_task_id_t               OsCreateTaskFence(OS_TASK_ENVIRONMENT *taskenv, OS_TASK_FENCE *fence, os_task_id_t const *dependency_list, size_t const dependency_count);

public_function unsigned int __cdecl       OsWorkerThreadMain(void *argp);
public_function size_t                     OsAllocationSizeForThreadPool(size_t thread_count);
public_function int                        OsCreateThreadPool(OS_THREAD_POOL *pool, OS_THREAD_POOL_INIT *init, OS_HOST_MEMORY_ARENA *arena, char const *name);
public_function void                       OsLaunchThreadPool(OS_THREAD_POOL *pool);
public_function void                       OsTerminateThreadPool(OS_THREAD_POOL *pool);
public_function void                       OsDestroyThreadPool(OS_THREAD_POOL *pool);
public_function bool                       OsSignalWorkerThreads(OS_WORKER_THREAD *sender, uintptr_t signal_arg, size_t thread_count);
public_function bool                       OsSignalWorkerThreads(OS_THREAD_POOL *pool, uintptr_t signal_arg, size_t thread_count);

public_function void                       OsResetInputSystem(OS_INPUT_SYSTEM *system);
public_function void                       OsPushRawInput(OS_INPUT_SYSTEM *system, RAWINPUT const *input);
public_function void                       OsPushRawInputDeviceChange(OS_INPUT_SYSTEM *system, WPARAM wparam, LPARAM lparam);
public_function void                       OsSimulateKeyPress(OS_INPUT_SYSTEM *system, HANDLE device, UINT virtual_key);
public_function void                       OsSimulateKeyRelease(OS_INPUT_SYSTEM *system, HANDLE device, UINT virtual_key);
public_function void                       OsConsumeInputEvents(OS_INPUT_EVENTS *events, OS_INPUT_SYSTEM *system, uint64_t tick_time);

public_function int                        OsEnumerateVulkanDrivers(OS_VULKAN_ICD_INFO *icd_list, size_t max_icds, size_t *icd_count);
public_function void                       OsFreeVulkanDriverList(OS_VULKAN_ICD_INFO *icd_list, size_t icd_count);
public_function VkResult                   OsLoadVulkanIcd(OS_VULKAN_RUNTIME_DISPATCH *runtime, OS_VULKAN_ICD_INFO *icd_info);
public_function VkResult                   OsLoadVulkanDriver(OS_VULKAN_RUNTIME_DISPATCH *runtime, HMODULE icd_module);
public_function VkResult                   OsLoadVulkanRuntime(OS_VULKAN_RUNTIME_DISPATCH *runtime);
public_function VkResult                   OsQueryVulkanRuntimeProperties(OS_VULKAN_RUNTIME_PROPERTIES *props, OS_VULKAN_RUNTIME_DISPATCH *runtime, OS_HOST_MEMORY_ARENA *arena);
public_function VkResult                   OsCreateVulkanInstance(OS_VULKAN_INSTANCE_DISPATCH *instance, OS_VULKAN_RUNTIME_DISPATCH *runtime, VkInstanceCreateInfo const *create_info, VkAllocationCallbacks const *allocation_callbacks);
public_function bool                       OsIsPrimaryDisplay(OS_VULKAN_PHYSICAL_DEVICE_LIST const *device_list, size_t display_index);
public_function int32_t                    OsDisplayRefreshRate(OS_VULKAN_PHYSICAL_DEVICE_LIST const *device_list, size_t display_index);
public_function char const*                OsSupportsVulkanInstanceLayer(OS_VULKAN_RUNTIME_PROPERTIES const *props, char const *layer_name, size_t *layer_index);
public_function bool                       OsSupportsAllVulkanInstanceLayers(OS_VULKAN_RUNTIME_PROPERTIES const *props, char const **layer_name, size_t const layer_count);
public_function char const*                OsSupportsVulkanInstanceExtension(OS_VULKAN_RUNTIME_PROPERTIES const *props, char const *extension_name, size_t *extension_index);
public_function bool                       OsSupportsAllVulkanInstanceExtensions(OS_VULKAN_RUNTIME_PROPERTIES const *props, char const **extension_names, size_t const extension_count);
public_function VkResult                   OsEnumerateVulkanPhysicalDevices(OS_VULKAN_PHYSICAL_DEVICE_LIST *device_list, OS_VULKAN_INSTANCE_DISPATCH *instance, OS_HOST_MEMORY_ARENA *arena, HINSTANCE exe_instance);
public_function VkResult                   OsCreateVulkanLogicalDevice(OS_VULKAN_DEVICE_DISPATCH *device, OS_VULKAN_INSTANCE_DISPATCH *instance, VkPhysicalDevice physical_Device, VkDeviceCreateInfo const *create_info, VkAllocationCallbacks const *allocation_callbacks);

public_function int                        OsInitializeAudio(OS_AUDIO_SYSTEM *audio_system);
public_function int                        OsEnumerateAudioDevices(OS_AUDIO_DEVICE_LIST *device_list, OS_AUDIO_SYSTEM *audio_system, OS_HOST_MEMORY_ARENA *arena);
public_function void                       OsDisableAudioOutput(OS_AUDIO_SYSTEM *audio_system);
public_function int                        OsEnableAudioOutput(OS_AUDIO_SYSTEM *audio_system, WCHAR *device_id, uint32_t samples_per_second, uint32_t buffer_size);
public_function int                        OsRecoverLostAudioOutputDevice(OS_AUDIO_SYSTEM *audio_system);
public_function uint32_t                   OsAudioSamplesToWrite(OS_AUDIO_SYSTEM *audio_system);
public_function int                        OsWriteAudioSamples(OS_AUDIO_SYSTEM *audio_system, void const *sample_data, uint32_t const sample_count);

public_function FILETIME                   OsUnixTimeToFILETIME(uint64_t unix_time_t);
public_function uint64_t                   OsFILETIMEtoUnixTime(FILETIME filetime);
public_function size_t                     OsShellFolderPath(WCHAR *buf, size_t buf_bytes, WCHAR **buf_end, size_t &bytes_needed, REFKNOWNFOLDERID folder_id);
public_function size_t                     OsKnownPath(WCHAR *buf, size_t buf_bytes, WCHAR **buf_end, size_t &bytes_needed, int folder_id);
public_function size_t                     OsNativePathForHandle(WCHAR *buf, size_t buf_bytes, WCHAR **buf_end, size_t &bytes_needed, HANDLE handle);
public_function size_t                     OsNativePathAppend(WCHAR *buf, size_t buf_bytes, WCHAR **buf_end, size_t &bytes_needed, WCHAR const *append);
public_function size_t                     OsNativePathChangeExtension(WCHAR *buf, size_t buf_bytes, WCHAR **buf_end, size_t &bytes_needed, WCHAR const *new_ext);
public_function size_t                     OsNativePathAppendExtension(WCHAR *buf, size_t buf_bytes, WCHAR **buf_end, size_t &bytes_needed, WCHAR const *new_ext);
public_function int                        OsNativePathParse(WCHAR *buf, WCHAR *buf_end, OS_PATH_PARTS *parts);
public_function size_t                     OsPhysicalSectorSize(HANDLE device);
public_function void                       OsInitFileSystemInfoChunkAllocator(OS_FSIC_ALLOCATOR *alloc);
public_function OS_FILE_INFO_CHUNK*        OsNewFileInfoChunk(OS_FSIC_ALLOCATOR *alloc);
public_function void                       OsFreeFileInfoChunkList(OS_FSIC_ALLOCATOR *alloc, OS_FILE_INFO_CHUNK *chunk);
public_function void                       OsLockFileInfoChunkRead(OS_FILE_INFO_CHUNK *chunk);
public_function void                       OsUnlockFileInfoChunkRead(OS_FILE_INFO_CHUNK *chunk);
public_function void                       OsLockFileInfoChunkWrite(OS_FILE_INFO_CHUNK *chunk);
public_function void                       OsUnlockFileInfoChunkWrite(OS_FILE_INFO_CHUNK *chunk);
public_function int                        OsOpenNativeDirectory(WCHAR const *fspath, HANDLE &dir);
public_function OS_FILE_INFO_CHUNK*        OsNativeDirectoryFindFiles(HANDLE dir, WCHAR const *filter, bool recurse, size_t &total_count, OS_FSIC_ALLOCATOR *alloc);
public_function void                       OsCloseNativeDirectory(HANDLE dir);
public_function int                        OsCreateNativeDirectory(WCHAR const *abspath); 
public_function int                        OsLoadFileData(OS_FILE_DATA *data, WCHAR const *path);
public_function int                        OsOpenFileMapping(OS_FILE_MAPPING *filemap, WCHAR const *path);
public_function void                       OsCloseFileMapping(OS_FILE_MAPPING *filemap);
public_function int                        OsMapFileRegion(OS_FILE_DATA *data, int64_t offset, int64_t size, OS_FILE_MAPPING *filemap);
public_function void                       OsFreeFileData(OS_FILE_DATA *data);

public_function size_t                     OsAllocationSizeForIoThreadPool(size_t thread_count);
public_function int                        OsCreateIoThreadPool(OS_IO_THREAD_POOL *pool, OS_IO_THREAD_POOL_INIT *init, OS_HOST_MEMORY_ARENA *arena, char const *name);
public_function void                       OsTerminateIoThreadPool(OS_IO_THREAD_POOL *pool);
public_function void                       OsDestroyIoThreadPool(OS_IO_THREAD_POOL *pool);
public_function size_t                     OsAllocationSizeForIoRequestPool(size_t pool_capacity);
public_function int                        OsCreateIoRequestPool(OS_IO_REQUEST_POOL *pool, OS_HOST_MEMORY_ARENA *arena, size_t pool_capacity);
public_function OS_IO_REQUEST*             OsAllocateIoRequest(OS_IO_REQUEST_POOL *pool);
public_function bool                       OsSubmitIoRequest(OS_IO_THREAD_POOL *pool, OS_IO_REQUEST *request);

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
/// @summary No-op stub function for XInputEnable.
/// @param enable If enable is FALSE XInput will only send neutral data in response to XInputGetState.
internal_function void WINAPI
XInputEnable_Stub
(
    BOOL enable
)
{
    UNREFERENCED_PARAMETER(enable);
}

/// @summary No-op stub function for XInputGetState.
/// @param dwUserIndex The index of the user's controller, in [0, 3].
/// @param pState Pointer to an XINPUT_STATE structure that receives the current state of the controller.
/// @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
internal_function DWORD WINAPI
XInputGetState_Stub
(
    DWORD        dwUserIndex, 
    XINPUT_STATE     *pState
)
{
    UNREFERENCED_PARAMETER(dwUserIndex);
    UNREFERENCED_PARAMETER(pState);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/// @summary No-op stub function for XInputSetState.
/// @param dwUserIndex The index of the user's controller, in [0, 3].
/// @param pVibration Pointer to an XINPUT_VIBRATION structure containing vibration information to send to the controller.
/// @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
internal_function DWORD WINAPI
XInputSetState_Stub
(
    DWORD            dwUserIndex, 
    XINPUT_VIBRATION *pVibration
)
{
    UNREFERENCED_PARAMETER(dwUserIndex);
    UNREFERENCED_PARAMETER(pVibration);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/// @summary No-op stub function for XInputGetCapabilities.
/// @param dwUserIndex The index of the user's controller, in [0, 3].
/// @param dwFlags Flags that identify the controller type, either 0 or XINPUT_FLAG_GAMEPAD.
/// @param pCapabilities Pointer to an XINPUT_CAPABILITIES structure that receives the controller capabilities.
/// @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
internal_function DWORD WINAPI
XInputGetCapabilities_Stub
(
    DWORD                  dwUserIndex, 
    DWORD                      dwFlags, 
    XINPUT_CAPABILITIES *pCapabilities
)
{
    UNREFERENCED_PARAMETER(dwUserIndex);
    UNREFERENCED_PARAMETER(dwFlags);
    UNREFERENCED_PARAMETER(pCapabilities);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/// @summary No-op stub function for XInputGetBatteryInformation.
/// @param dwUserIndex The index of the user's controller, in [0, 3].
/// @param devType Specifies which device associated with this user index should be queried. One of BATTERY_DEVTYPE_GAMEPAD or BATTERY_DEVTYPE_HEADSET.
/// @param Pointer to an XINPUT_BATTERY_INFORMATION structure that receives the battery information.
/// @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
/*internal_function DWORD WINAPI
XInputGetBatteryInformation_Stub
(
    DWORD                               dwUserIndex, 
    BYTE                                    devType, 
    XINPUT_BATTERY_INFORMATION *pBatteryInformation
)
{
    UNREFERENCED_PARAMETER(dwUserIndex);
    UNREFERENCED_PARAMETER(devType);
    UNREFERENCED_PARAMETER(pBatteryInformation);
    return ERROR_DEVICE_NOT_CONNECTED;
}*/

/// @summary Make a signed 64-bit integer from two DWORDs.
/// @param high32 The upper 32 bits of the 64-bit value.
/// @param low32 The lower 32 bits of the 64-bit value.
/// @return The signed 64-bit integer value.
internal_function inline int64_t
OsMakeInt64
(
    uint32_t high32, 
    uint32_t low32
)
{
    return (((int64_t) high32) << 32) | ((int64_t) low32);
}

/// @summary Make an unsigned 64-bit integer from two DWORDs.
/// @param high32 The upper 32 bits of the 64-bit value.
/// @param low32 The lower 32 bits of the 64-bit value.
/// @return The unsigned 64-bit integer value.
internal_function inline uint64_t
OsMakeUInt64
(
    uint32_t high32, 
    uint32_t low32
)
{
    return (((uint64_t) high32) << 32) | ((uint64_t) low32);
}

/// @summary Retrieve the next power-of-two greater than or equal to a given value.
/// @param n The input value.
/// @return An integer k such that k >= n and is a power of two.
internal_function size_t
OsNextPowerOfTwoGreaterOrEqual
(
    size_t n
)
{
    --n;
    for (size_t i = 1, k = sizeof(size_t) * CHAR_BIT; i < k; i <<= 1)
    {
        n |= n >> i;
    }
    return n+1;
}

/// @summary Given two timestamp values, calculate the number of nanoseconds between them.
/// @param start_ticks The TimestampInTicks at the beginning of the measured interval.
/// @param end_ticks The TimestampInTicks at the end of the measured interval.
/// @param frequency The number of ticks-per-second of the clock used to capture the timestamp values.
/// @return The elapsed time between the timestamps, specified in nanoseconds.
internal_function uint64_t
OsElapsedNanoseconds
(
    uint64_t start_ticks, 
    uint64_t   end_ticks, 
    int64_t    frequency
)
{   // scale the tick value by the nanoseconds-per-second multiplier
    // before scaling back down by ticks-per-second to avoid loss of precision.
    return (1000000000ULL * (end_ticks - start_ticks)) / uint64_t(frequency);
}

/// @summary Enable or disable a process privilege.
/// @param token The privilege token of the process to modify.
/// @param privilege_name The name of the privilege to enable or disable.
/// @param should_enable Specify TRUE to request the privilege, or FALSE to disable the privilege.
internal_function bool
OsEnableProcessPrivilege
(
    HANDLE           token, 
    LPCTSTR privilege_name,
    BOOL     should_enable
)
{
    TOKEN_PRIVILEGES tp;
    LUID           luid;

    if (LookupPrivilegeValue(NULL, privilege_name, &luid))
    {
        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid;
        tp.Privileges[0].Attributes = should_enable ? SE_PRIVILEGE_ENABLED : 0;
        if (AdjustTokenPrivileges(token, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL))
        {   // the requested privilege adjustment was made successfully.
            return (GetLastError() != ERROR_NOT_ALL_ASSIGNED);
        }
    }
    return false;
}

/// @summary Request any privilege elevations for the current process.
/// @return true if the necessary privileges have been obtained.
internal_function bool
OsElevateProcessPrivileges
(
    void
)
{
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token))
    {   // request elevated privileges one at a time.
        bool se_debug       = OsEnableProcessPrivilege(token, SE_DEBUG_NAME, TRUE);
        bool se_volume_name = OsEnableProcessPrivilege(token, SE_MANAGE_VOLUME_NAME, TRUE);
        // ...
        CloseHandle(token);
        return (se_debug && se_volume_name);
    }
    return false;
}

/// @summary Load the latest version of XInput into the process address space and resolve the required API functions.
/// @param missing_entry_points On return, set to true if any entry points are missing.
/// @return true if the XInput DLL was loaded into the process address space.
internal_function bool
OsLoadXInput
(
    bool *missing_entry_points
)
{   // start by trying to load the most recent version of the DLL, which ships with the Windows 8 SDK.
    HMODULE xinput_dll = NULL;
    if ((xinput_dll = LoadLibrary(_T("xinput1_4.dll"))) == NULL)
    {   // try with XInput 9.1.0, which shipped starting with Windows Vista.
        if ((xinput_dll = LoadLibrary(_T("xinput9_1_0.dll"))) == NULL)
        {   // try for XInput 1.3, which shipped in the June 2010 DirectX SDK.
            if ((xinput_dll = LoadLibrary(_T("xinput1_3.dll"))) == NULL)
            {   // no XInput is available, so resolve everything to the stub functions.
                if (missing_entry_points) *missing_entry_points = true;
                return false;
            }
        }
    }

    // perform runtime resolution of all required API functions.
    OS_LAYER_RESOLVE_RUNTIME_FUNCTION(xinput_dll, XInputEnable);
    OS_LAYER_RESOLVE_RUNTIME_FUNCTION(xinput_dll, XInputGetState);
    OS_LAYER_RESOLVE_RUNTIME_FUNCTION(xinput_dll, XInputSetState);
    OS_LAYER_RESOLVE_RUNTIME_FUNCTION(xinput_dll, XInputGetCapabilities);
    //OS_LAYER_RESOLVE_RUNTIME_FUNCTION(xinput_dll, XInputGetBatteryInformation);

    // check for any entry points that got set to their stub functions.
    if (OS_LAYER_RUNTIME_STUB(XInputEnable))                goto missing_entry_point;
    if (OS_LAYER_RUNTIME_STUB(XInputGetState))              goto missing_entry_point;
    if (OS_LAYER_RUNTIME_STUB(XInputSetState))              goto missing_entry_point;
    if (OS_LAYER_RUNTIME_STUB(XInputGetCapabilities))       goto missing_entry_point;
    //if (OS_LAYER_RUNTIME_STUB(XInputGetBatteryInformation)) goto missing_entry_point;

    // save the DLL handle.
    XInputDll = xinput_dll;
    return true;

missing_entry_point:
    if (missing_entry_points) *missing_entry_points = true;
    XInputDll = xinput_dll;
    return true;
}

/// @summary Search a device list for a device with a given handle.
/// @param device_list The device list to search.
/// @param device_handle The handle of the device to locate.
/// @return The zero-based index of the device in the device list, or OS_INPUT_DEVICE_NOT_FOUND.
template <typename T>
internal_function size_t 
OsFindInputDeviceForHandle
(
    OS_INPUT_DEVICE_LIST<T> *device_list, 
    HANDLE                 device_handle
)
{
    for (size_t i = 0, n = device_list->DeviceCount; i < n; ++i)
    {
        if (device_list->DeviceHandle[i] == device_handle)
            return i;
    }
    return OS_INPUT_DEVICE_NOT_FOUND;
}

/// @summary Adds a device to a device list.
/// @param devices The list of devices to search.
/// @param device The handle of the device to insert.
/// @param default_state The initial device state to associate with the device, if the device was not already present in the device list.
/// @return The zero-based index of the device in the list, or OS_INPUT_DEVICE_TOO_MANY.
template <typename T>
internal_function size_t
OsDeviceAttached
(
    OS_INPUT_DEVICE_LIST<T> *devices,
    HANDLE                    device,
    T const           &default_state
)
{
    size_t index = 0;

    // search for the device in the device list to avoid duplicate devices.
    for (size_t i = 0, n = devices->DeviceCount; i < n; ++i)
    {
        if (devices->DeviceHandle[i] == device)
        {   // the device is already present in the device list. don't modify its state.
            return i;
        }
    }
    // the device isn't present in the device list, so add it.
    if (devices->DeviceCount == OS_INPUT_DEVICE_LIST<T>::MAX_DEVICES)
    {   // there are too many devices of the specified type attached; cannot add.
        return OS_INPUT_DEVICE_TOO_MANY;
    }
    index = devices->DeviceCount++;
    devices->DeviceHandle[index] = device;
    devices->DeviceState [index] = default_state;
    return index;
}

/// @summary Removes a device from a device list.
/// @param devices The list of devices to search.
/// @param device The handle of the device to remove.
/// @return true if the device was found in the device list.
template <typename T>
internal_function bool
OsDeviceRemoved
(
    OS_INPUT_DEVICE_LIST<T> *devices,
    HANDLE                    device 
)
{   // locate the device in the device list.
    for (size_t i = 0, n = devices->DeviceCount; i < n; ++i)
    {
        if (devices->DeviceHandle[i] == device)
        {   // the device has been located. swap the last item into its slot.
            size_t   last_index = devices->DeviceCount - 1;
            if (i != last_index)
            {
                devices->DeviceHandle[i] = devices->DeviceHandle[last_index];
                devices->DeviceState [i] = devices->DeviceState [last_index];
            }
            devices->DeviceCount--;
            return true;
        }
    }
    return false;
}

/// @summary Given a Raw Input keyboard packet, retrieve the virtual key code and scan code values.
/// @param key The Raw Input keyboard packet to process.
/// @param vkey_code On return, stores the virtual key code identifier (always less than or equal to 255.)
/// @param scan_code On return, stores the scan code value, suitable for passing to CopyKeyDisplayName.
/// @return true if vkey_code and scan_code were set, or false if the packet was part of an escape sequence.
internal_function bool
OsGetVirtualKeyAndScanCode
(
    RAWKEYBOARD const &key, 
    uint32_t    &vkey_code, 
    uint32_t    &scan_code
)
{
    uint32_t vkey =  key.VKey;
    uint32_t scan =  key.MakeCode;
    bool       e0 =((key.Flags & RI_KEY_E0) != 0);

    if (vkey == 255)
    {   // discard fake keys; these are just part of an escaped sequence.
        vkey_code = 0; scan_code = 0;
        return false;
    }
    if (vkey == VK_SHIFT)
    {   // correct left/right shift.
        vkey  = MapVirtualKey(scan, MAPVK_VSC_TO_VK_EX);
    }
    if (vkey == VK_NUMLOCK)
    {   // correct PAUSE/BREAK and NUMLOCK. set the extended bit.
        scan  = MapVirtualKey(vkey, MAPVK_VK_TO_VSC) | 0x100;
    }
    if (key.Flags & RI_KEY_E1)
    {   // for escaped sequences, turn the virtual key into the correct scan code.
        // unfortunately, MapVirtualKey can't handle VK_PAUSE, so do that manually.
        if (vkey != VK_PAUSE) scan = MapVirtualKey(vkey, MAPVK_VK_TO_VSC);
        else scan = 0x45;
    }
    switch (vkey)
    {   // map left/right versions of various keys.
        case VK_CONTROL:  /* left/right CTRL */
            vkey =  e0 ? VK_RCONTROL : VK_LCONTROL;
            break;
        case VK_MENU:     /* left/right ALT  */
            vkey =  e0 ? VK_RMENU : VK_LMENU;
            break;
        case VK_RETURN:
            vkey =  e0 ? VK_SEPARATOR : VK_RETURN;
            break;
        case VK_INSERT:
            vkey = !e0 ? VK_NUMPAD0 : VK_INSERT;
            break;
        case VK_DELETE:
            vkey = !e0 ? VK_DECIMAL : VK_DELETE;
            break;
        case VK_HOME:
            vkey = !e0 ? VK_NUMPAD7 : VK_HOME;
            break;
        case VK_END:
            vkey = !e0 ? VK_NUMPAD1 : VK_END;
            break;
        case VK_PRIOR:
            vkey = !e0 ? VK_NUMPAD9 : VK_PRIOR;
            break;
        case VK_NEXT:
            vkey = !e0 ? VK_NUMPAD3 : VK_NEXT;
            break;
        case VK_LEFT:
            vkey = !e0 ? VK_NUMPAD4 : VK_LEFT;
            break;
        case VK_RIGHT:
            vkey = !e0 ? VK_NUMPAD6 : VK_RIGHT;
            break;
        case VK_UP:
            vkey = !e0 ? VK_NUMPAD8 : VK_UP;
            break;
        case VK_DOWN:
            vkey = !e0 ? VK_NUMPAD2 : VK_DOWN;
            break;
        case VK_CLEAR:
            vkey = !e0 ? VK_NUMPAD5 : VK_CLEAR;
            break;
    }

    // return the processed codes back to the caller.
    vkey_code = vkey;
    scan_code = scan;
    return true;
}

/// @summary Retrieve the localized display name for a keyboard scan code.
/// @param scan_code The key scan code, as returned by ProcessKeyboardPacket.
/// @param ri_flags The RAWKEYBOARD::Flags field from the input packet.
/// @param buffer The caller-managed buffer into which the display name will be copied.
/// @param buffer_size The maximum number of characters that can be written to the buffer.
/// @return The number of characters written to the buffer, not including the zero codepoint, or 0.
internal_function size_t
OsCopyKeyDisplayName
(
    uint32_t   scan_code, 
    uint32_t    ri_flags, 
    TCHAR        *buffer, 
    size_t   buffer_size
)
{
    LONG key_code = (scan_code << 16) | (((ri_flags & RI_KEY_E0) ? 1 : 0) << 24);
    return (size_t)  GetKeyNameText(key_code, buffer, (int) buffer_size);
}

/// @summary Process a Raw Input keyboard packet to update the state of a keyboard device.
/// @param input The Raw Input keyboard packet to process.
/// @param devices The list of keyboard devices to update.
/// @return The zero-based index of the input device in the device list, or OS_INPUT_DEVICE_TOO_MANY.
internal_function size_t
OsProcessKeyboardPacket
(
    RAWINPUT const     *input,
    OS_KEYBOARD_LIST *devices
)
{
    RAWINPUTHEADER const  &header =  input->header;
    RAWKEYBOARD    const     &key =  input->data.keyboard;
    OS_KEYBOARD_STATE      *state =  NULL;
    size_t                  index =  0;
    uint32_t                 vkey =  0;
    uint32_t                 scan =  0;

    // locate the keyboard in the current state buffer by device handle.
    for (size_t i = 0, n = devices->DeviceCount; i < n; ++i)
    {
        if (devices->DeviceHandle[i] == header.hDevice)
        {   // found the matching device.
            index = i;
            state =&devices->DeviceState[i];
            break;
        }
    }
    if (state == NULL)
    {   // this device was newly attached.
        if (devices->DeviceCount == OS_KEYBOARD_LIST::MAX_DEVICES)
        {   // there are too many devices of the specified type attached.
            return OS_INPUT_DEVICE_TOO_MANY;
        }
        index  = devices->DeviceCount++;
        state  =&devices->DeviceState[index];
        devices->DeviceHandle[index] = header.hDevice;
        devices->DeviceState [index] = OS_KEYBOARD_STATE_STATIC_INIT;
    }
    if (!OsGetVirtualKeyAndScanCode(key, vkey, scan))
    {   // discard fake keys; these are just part of an escaped sequence.
        return index;
    }
    if ((key.Flags & RI_KEY_BREAK) == 0)
    {   // the key is currently pressed; set the bit corresponding to the virtual key code.
        state->KeyState[vkey >> 5] |= (1UL << (vkey & 0x1F));
    }
    else
    {   // the key was just released; clear the bit corresponding to the virtual key code.
        state->KeyState[vkey >> 5] &=~(1UL << (vkey & 0x1F));
    }
    return index;
}

/// @summary Process a Raw Input mouse packet to update the state of a pointing device.
/// @param input The Raw Input mouse packet to process.
/// @param devices The list of pointing devices to update.
/// @return The zero-based index of the input device in the device list, or OS_INPUT_DEVICE_TOO_MANY.
internal_function size_t
OsProcessPointerPacket
(
    RAWINPUT const    *input,
    OS_POINTER_LIST *devices
)
{
    RAWINPUTHEADER const &header = input->header;
    RAWMOUSE       const  &mouse = input->data.mouse;
    OS_POINTER_STATE      *state = NULL;
    size_t                 index = 0;
    POINT                 cursor = {};

    // locate the pointer in the current state buffer by device handle.
    for (size_t i = 0, n = devices->DeviceCount; i < n; ++i)
    {
        if (devices->DeviceHandle[i] == header.hDevice)
        {   // found the matching device.
            index = i;
            state =&devices->DeviceState[i];
            break;
        }
    }
    if (state == NULL)
    {   // this device was newly attached.
        if (devices->DeviceCount == OS_POINTER_LIST::MAX_DEVICES)
        {   // there are too many devices of the specified type attached.
            return OS_INPUT_DEVICE_TOO_MANY;
        }
        index  = devices->DeviceCount++;
        state  =&devices->DeviceState[index];
        devices->DeviceHandle[index] = header.hDevice;
        devices->DeviceState [index] = OS_POINTER_STATE_STATIC_INIT;
    }

    // grab the current mouse pointer position, in pixels.
    GetCursorPos(&cursor);
    state->Pointer[0] = cursor.x;
    state->Pointer[1] = cursor.y;

    if (mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
    {   // the device is a pen, touchscreen, etc. and specifies absolute coordinates.
        state->Relative[0]  = mouse.lLastX;
        state->Relative[1]  = mouse.lLastY;
        state->Flags = OS_POINTER_FLAGS_ABSOLUTE;
    }
    else
    {   // the device has specified relative coordinates in mickeys.
        state->Relative[0] += mouse.lLastX;
        state->Relative[1] += mouse.lLastY;
        state->Flags = OS_POINTER_FLAGS_NONE;
    }
    if (mouse.usButtonFlags & RI_MOUSE_WHEEL)
    {   // mouse wheel data was supplied with the packet.
        state->Relative[2] = (int16_t) mouse.usButtonData;
    }
    else
    {   // no mouse wheel data was supplied with the packet.
        state->Relative[2] = 0;
    }

    // rebuild the button state vector. Raw Input supports up to 5 buttons.
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) state->Buttons |=  MK_LBUTTON;
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP  ) state->Buttons &= ~MK_LBUTTON;
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN) state->Buttons |=  MK_RBUTTON;
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP  ) state->Buttons &= ~MK_RBUTTON;
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_3_DOWN) state->Buttons |=  MK_MBUTTON;
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_3_UP  ) state->Buttons &= ~MK_MBUTTON;
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) state->Buttons |=  MK_XBUTTON1;
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP  ) state->Buttons &= ~MK_XBUTTON1;
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) state->Buttons |=  MK_XBUTTON2;
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP  ) state->Buttons &= ~MK_XBUTTON2;
    return index;
}

/// @summary Apply scaled radial deadzone logic to an analog stick input.
/// @param stick_x The x-axis component of the analog input.
/// @param stick_y The y-axis component of the analog input.
/// @param deadzone The deadzone size as a percentage of the total input range.
/// @param stick_xymn A four-element array that will store the normalized x- and y-components of the input direction, the magnitude, and the normalized magnitude.
internal_function void
OsScaledRadialDeadzone
(
    int16_t    stick_x, 
    int16_t    stick_y, 
    float     deadzone, 
    float  *stick_xymn
)
{
    float  x = stick_x;
    float  y = stick_y;
    float  m = sqrtf(x * x + y * y);
    float nx = x / m;
    float ny = y / m;

    if (m < deadzone)
    {   // drop the input; it falls within the deadzone.
        stick_xymn[0] = 0;
        stick_xymn[1] = 0;
        stick_xymn[2] = 0;
        stick_xymn[3] = 0;
    }
    else
    {   // rescale the input into the non-dead space.
        float n = (m - deadzone) / (1.0f - deadzone);
        stick_xymn[0] = nx * n;
        stick_xymn[1] = ny * n;
        stick_xymn[2] = m;
        stick_xymn[3] = n;
    }
}

/// @summary Process an XInput gamepad packet to apply deadzone logic and update button states.
/// @param input The XInput gamepad packet to process.
/// @param port_index The zero-based index of the port to which the gamepad is connected.
/// @param devices The list of gamepad devices to update.
/// @return The zero-based index of the input device in the device list, or OS_INPUT_DEVICE_TOO_MANY.
internal_function size_t
OsProcessGamepadPacket
(
    XINPUT_STATE const *input, 
    DWORD          port_index, 
    OS_GAMEPAD_LIST  *devices
)
{
    OS_GAMEPAD_STATE *state = NULL;
    uintptr_t       pDevice =(uintptr_t) port_index;
    HANDLE          hDevice =(HANDLE) pDevice;
    size_t            index = 0;

    // locate the pointer in the current state buffer by port index.
    for (size_t i = 0, n = devices->DeviceCount; i < n; ++i)
    {
        if (devices->DeviceHandle[i] == hDevice)
        {   // found the matching device.
            index = i;
            state =&devices->DeviceState[i];
            break;
        }
    }
    if (state == NULL)
    {   // this device was newly attached.
        if (devices->DeviceCount == OS_GAMEPAD_LIST::MAX_DEVICES)
        {   // there are too many devices of the specified type attached.
            return OS_INPUT_DEVICE_TOO_MANY;
        }
        index  = devices->DeviceCount++;
        state  =&devices->DeviceState[index];
        devices->DeviceHandle[index] = hDevice;
        devices->DeviceState [index] = OS_GAMEPAD_STATE_STATIC_INIT;
    }

    // apply deadzone filtering to the trigger inputs.
    state->LTrigger = input->Gamepad.bLeftTrigger  > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? input->Gamepad.bLeftTrigger  : 0;
    state->RTrigger = input->Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? input->Gamepad.bRightTrigger : 0;
    // copy over the button state bits as-is.
    state->Buttons  = input->Gamepad.wButtons;
    // apply deadzone filtering to the analog stick inputs.
    float const l_deadzone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  / 32767.0f;
    float const r_deadzone = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32767.0f;
    OsScaledRadialDeadzone(input->Gamepad.sThumbLX, input->Gamepad.sThumbLY, l_deadzone, state->LStick);
    OsScaledRadialDeadzone(input->Gamepad.sThumbRX, input->Gamepad.sThumbRY, r_deadzone, state->RStick);
    return index;
}

/// @summary Poll XInput gamepads attached to the system and update the input device state.
/// @param devices The list of gamepad devices to update.
/// @param ports_in A bitvector specifying the gamepad ports to poll. Specify OS_ALL_GAMEPAD_PORTS to poll all possible ports. MSDN recommends against polling unattached ports each frame.
/// @param ports_out A bitvector specifying the attached gamepad ports. Bit x is set if port x has an attached gamepad.
/// @return The number of gamepads attached to the system (the number of bits set in ports_out.)
internal_function size_t
OsPollGamepads
(
    OS_GAMEPAD_LIST *devices,
    uint32_t        ports_in,
    uint32_t      &ports_out
)
{
    size_t   const max_gamepads = 4;
    size_t         num_gamepads = 0;
    uint32_t const port_bits[4] = {
        (1UL << 0UL), 
        (1UL << 1UL), 
        (1UL << 2UL), 
        (1UL << 3UL)
    };
    // clear all attached port bits in the output:
    ports_out = 0;
    // poll any ports whose corresponding bit is set in ports_in.
    for (size_t i = 0; i < max_gamepads; ++i)
    {
        if (ports_in & port_bits[i])
        {
            XINPUT_STATE state = {};
            DWORD       result = XInputGetState_Func((DWORD) i, &state);
            if (result == ERROR_SUCCESS)
            {   // gamepad port i is attached and in use.
                ports_out |= port_bits[i];
                // update the corresponding input state.
                OsProcessGamepadPacket(&state, (DWORD) i, devices);
                num_gamepads++;
            }
        }
    }
    return num_gamepads;
}

/// @summary Given a two device list snapshots, populate a device set specifying whether a given device is in none, one or both snapshots.
/// @param set The device set to populate. This set should be initialized with OS_INPUT_DEVICE_SET_STATIC_INIT prior to calling this function.
/// @param prev The device list from the previous state snapshot.
/// @param curr The device list from the current state snapshot.
template <typename T>
internal_function void
OsDetermineDeviceSet
(
    OS_INPUT_DEVICE_SET      *set, 
    OS_INPUT_DEVICE_LIST<T> *prev,
    OS_INPUT_DEVICE_LIST<T> *curr
)
{   assert(set->DeviceCount == 0);
    // build a table of 'unique' devices and which set(s) they belong to.
    set->DeviceCount = prev->DeviceCount;
    for (size_t i = 0, n = prev->DeviceCount; i < n; ++i)
    {   // the table starts out empty, so this step is just a simple copy.
        set->DeviceIds [i] = prev->DeviceHandle[i];
        set->Membership[i] = OS_INPUT_DEVICE_MEMBERSHIP_PREV;
        set->PrevIndex [i] =(uint8_t) i;
    }
    for (size_t i = 0, n = curr->DeviceCount; i < n; ++i)
    {   // the table may not be empty, so filter out duplicate devices.
        HANDLE id = curr->DeviceHandle[i];
        size_t ix = set->DeviceCount;
        size_t in = 1;
        for (size_t j = 0, m = set->DeviceCount; j < m; ++j)
        {
            if (set->DeviceIds[j] == id)
            {   // found an existing entry with the same handle.
                ix = j; // update the existing entry.
                in = 0; // don't increment the device count.
                break;
            }
        }
        set->DeviceIds [ix]  = id;
        set->Membership[ix] |= OS_INPUT_DEVICE_MEMBERSHIP_CURR;
        set->CurrIndex [ix]  =(uint8_t) i;
        set->DeviceCount    += in;
    }
}

/// @summary Given two keyboard state snapshots, generate keyboard events for keys down, pressed and released.
/// @param keys The keyboard events data to populate.
/// @param prev The previous keyboard state snapshot.
/// @param curr The current keyboard state snapshot.
internal_function void
OsGenerateKeyboardInputEvents
(
    OS_KEYBOARD_EVENTS      *keys,
    OS_KEYBOARD_STATE const *prev, 
    OS_KEYBOARD_STATE const *curr
)
{
    keys->DownCount     = 0;
    keys->PressedCount  = 0;
    keys->ReleasedCount = 0;
    for (size_t i = 0; i < 8; ++i)
    {
        uint32_t curr_state = curr->KeyState[i];
        uint32_t prev_state = prev->KeyState[i];
        uint32_t    changes =(curr_state ^  prev_state);
        uint32_t      downs =(changes    &  curr_state);
        uint32_t        ups =(changes    & ~curr_state);

        for (uint32_t j = 0; j < 32; ++j)
        {
            uint32_t mask = (1 << j);
            uint8_t  vkey = (uint8_t) ((i << 5) + j);

            if ((curr_state & mask) != 0 && keys->DownCount < OS_KEYBOARD_EVENTS::MAX_KEYS)
            {   // this key is currently pressed.
                keys->Down[keys->DownCount++] = vkey;
            }
            if ((downs & mask) != 0 && keys->PressedCount < OS_KEYBOARD_EVENTS::MAX_KEYS)
            {   // this key was just pressed.
                keys->Pressed[keys->PressedCount++] = vkey;
            }
            if ((ups & mask) != 0 && keys->ReleasedCount < OS_KEYBOARD_EVENTS::MAX_KEYS)
            {   // this key was just released.
                keys->Released[keys->ReleasedCount++] = vkey;
            }
        }
    }
}

/// @summary Generate keyboard device and input events for all keyboard devices given two device list snapshots.
/// @param events The input events structure to populate.
/// @param prev The device list from the previous tick.
/// @param curr The device list from the current tick.
internal_function void
OsGenerateKeyboardEvents
(
    OS_INPUT_EVENTS *events, 
    OS_KEYBOARD_LIST  *prev,
    OS_KEYBOARD_LIST  *curr
)
{
    OS_INPUT_DEVICE_SET device_set = OS_INPUT_DEVICE_SET_STATIC_INIT;

    // determine the device set, which is used to determine whether devices were added or removed, 
    // and also whether or not (or how) to generate state-based input events for a given device.
    OsDetermineDeviceSet(&device_set, prev, curr);

    // loop through each individual device and emit events.
    events->KeyboardAttachCount = 0;
    events->KeyboardRemoveCount = 0;
    events->KeyboardCount       = 0;
    for (size_t i = 0, n = device_set.DeviceCount; i < n; ++i)
    {
        switch (device_set.Membership[i])
        {
            case OS_INPUT_DEVICE_MEMBERSHIP_PREV:
                {   // this input device was just removed. no device state events are generated.
                    events->KeyboardRemove[events->KeyboardRemoveCount] = device_set.DeviceIds[i];
                    events->KeyboardRemoveCount++;
                } break;

            case OS_INPUT_DEVICE_MEMBERSHIP_CURR:
                {   // this input device was just attached. no device state events are generated.
                    events->KeyboardAttach[events->KeyboardAttachCount] = device_set.DeviceIds[i];
                    events->KeyboardAttachCount++;
                } break;

            case OS_INPUT_DEVICE_MEMBERSHIP_BOTH:
                {   // this input device was present in both snapshots.
                    OS_KEYBOARD_EVENTS *input_ev = &events->KeyboardEvents[events->KeyboardCount];
                    OS_KEYBOARD_STATE  *state_pp = &prev->DeviceState[device_set.PrevIndex[i]];
                    OS_KEYBOARD_STATE  *state_cp = &curr->DeviceState[device_set.CurrIndex[i]];
                    events->KeyboardIds[events->KeyboardCount] = device_set.DeviceIds[i];
                    OsGenerateKeyboardInputEvents(input_ev, state_pp, state_cp);
                    events->KeyboardCount++;
                } break;
        }
    }
}

/// @summary Given two pointer device state snapshots, generate events for buttons down, pressed and released.
/// @param pointer The pointer device events data to populate.
/// @param prev The previous pointer device state snapshot.
/// @param curr The current pointer device state snapshot.
internal_function void
OsGeneratePointerInputEvents
(
    OS_POINTER_EVENTS   *pointer, 
    OS_POINTER_STATE const *prev, 
    OS_POINTER_STATE const *curr
)
{   // copy cursor and wheel data, and generate relative data.
    pointer->Cursor[0]  = curr->Pointer[0];
    pointer->Cursor[1]  = curr->Pointer[1];
    pointer->WheelDelta = curr->Relative[2];
    if (curr->Flags & OS_POINTER_FLAGS_ABSOLUTE)
    {   // calculate relative values as the delta between states.
        pointer->Mickeys[0] = curr->Relative[0] - prev->Relative[0];
        pointer->Mickeys[1] = curr->Relative[1] - prev->Relative[1];
    }
    else
    {   // the driver specified relative values; copy them as-is.
        pointer->Mickeys[0] = curr->Relative[0];
        pointer->Mickeys[1] = curr->Relative[1];
    }

    uint32_t curr_state = curr->Buttons;
    uint32_t prev_state = prev->Buttons;
    uint32_t    changes =(curr_state ^  prev_state);
    uint32_t      downs =(changes    &  curr_state);
    uint32_t        ups =(changes    & ~curr_state);

    // generate the button events. check each bit in the buttons mask.
    pointer->DownCount     = 0;
    pointer->PressedCount  = 0;
    pointer->ReleasedCount = 0;
    for (uint32_t i = 0; i < 16; ++i)
    {
        uint32_t mask   = (1UL << i);
        uint16_t button = (uint16_t) mask;

        if ((curr_state & mask) != 0 && pointer->DownCount < OS_POINTER_EVENTS::MAX_BUTTONS)
        {   // this button is currently pressed.
            pointer->Down[pointer->DownCount++] = button;
        }
        if ((downs & mask) != 0 && pointer->PressedCount < OS_POINTER_EVENTS::MAX_BUTTONS)
        {   // this button was just pressed.
            pointer->Pressed[pointer->PressedCount++] = button;
        }
        if ((ups & mask) != 0 && pointer->ReleasedCount < OS_POINTER_EVENTS::MAX_BUTTONS)
        {   // this button was just released.
            pointer->Released[pointer->ReleasedCount++] = button;
        }
    }
}

/// @summary Generate pointer device and input events for all pointer devices given two device list snapshots.
/// @param events The input events structure to populate.
/// @param prev The device list from the previous tick.
/// @param curr The device list from the current tick.
internal_function void
OsGeneratePointerEvents
(
    OS_INPUT_EVENTS *events, 
    OS_POINTER_LIST   *prev,
    OS_POINTER_LIST   *curr
)
{
    OS_INPUT_DEVICE_SET device_set = OS_INPUT_DEVICE_SET_STATIC_INIT;

    // determine the device set, which is used to determine whether devices were added or removed, 
    // and also whether or not (or how) to generate state-based input events for a given device.
    OsDetermineDeviceSet(&device_set, prev, curr);

    // loop through each individual device and emit events.
    events->PointerAttachCount = 0;
    events->PointerRemoveCount = 0;
    events->PointerCount       = 0;
    for (size_t i = 0, n = device_set.DeviceCount; i < n; ++i)
    {
        switch (device_set.Membership[i])
        {
            case OS_INPUT_DEVICE_MEMBERSHIP_PREV:
                {   // this input device was just removed. no device state events are generated.
                    events->PointerRemove[events->PointerRemoveCount] = device_set.DeviceIds[i];
                    events->PointerRemoveCount++;
                } break;

            case OS_INPUT_DEVICE_MEMBERSHIP_CURR:
                {   // this input device was just attached. no device state events are generated.
                    events->PointerAttach[events->PointerAttachCount] = device_set.DeviceIds[i];
                    events->PointerAttachCount++;
                } break;

            case OS_INPUT_DEVICE_MEMBERSHIP_BOTH:
                {   // this input device was present in both snapshots.
                    OS_POINTER_EVENTS *input_ev = &events->PointerEvents[events->PointerCount];
                    OS_POINTER_STATE  *state_pp = &prev->DeviceState[device_set.PrevIndex[i]];
                    OS_POINTER_STATE  *state_cp = &curr->DeviceState[device_set.CurrIndex[i]];
                    events->PointerIds[events->PointerCount] = device_set.DeviceIds[i];
                    OsGeneratePointerInputEvents(input_ev, state_pp, state_cp);
                    events->PointerCount++;
                } break;
        }
    }
}

/// @summary Given two gamepad device state snapshots, generate events for buttons down, pressed and released.
/// @param pointer The gamepad device events data to populate.
/// @param prev The previous gamepad device state snapshot.
/// @param curr The current gamepad device state snapshot.
internal_function void
OsGenerateGamepadInputEvents
(
    OS_GAMEPAD_EVENTS   *gamepad, 
    OS_GAMEPAD_STATE const *prev,
    OS_GAMEPAD_STATE const *curr
)
{
    gamepad->LeftTrigger         = (float) curr->LTrigger / (float) (255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    gamepad->RightTrigger        = (float) curr->RTrigger / (float) (255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    gamepad->LeftStick[0]        =  curr->LStick[0];
    gamepad->LeftStick[1]        =  curr->LStick[1];
    gamepad->LeftStickMagnitude  =  curr->LStick[3];
    gamepad->RightStick[0]       =  curr->RStick[0];
    gamepad->RightStick[1]       =  curr->RStick[1];
    gamepad->RightStickMagnitude =  curr->RStick[3];

    uint32_t curr_state = curr->Buttons;
    uint32_t prev_state = prev->Buttons;
    uint32_t    changes =(curr_state ^  prev_state);
    uint32_t      downs =(changes    &  curr_state);
    uint32_t        ups =(changes    & ~curr_state);

    // generate the button events. check each bit in the buttons mask.
    gamepad->DownCount     = 0;
    gamepad->PressedCount  = 0;
    gamepad->ReleasedCount = 0;
    for (uint32_t i = 0; i < 16; ++i)
    {
        uint32_t mask   = (1UL << i);
        uint16_t button = (uint16_t) mask;

        if ((curr_state & mask) != 0 && gamepad->DownCount < OS_GAMEPAD_EVENTS::MAX_BUTTONS)
        {   // this button is currently pressed.
            gamepad->Down[gamepad->DownCount++] = button;
        }
        if ((downs & mask) != 0 && gamepad->PressedCount < OS_GAMEPAD_EVENTS::MAX_BUTTONS)
        {   // this button was just pressed.
            gamepad->Pressed[gamepad->PressedCount++] = button;
        }
        if ((ups & mask) != 0 && gamepad->ReleasedCount < OS_GAMEPAD_EVENTS::MAX_BUTTONS)
        {   // this button was just released.
            gamepad->Released[gamepad->ReleasedCount++] = button;
        }
    }
}

/// @summary Generate gamepad device and input events for all gamepad devices given two device list snapshots.
/// @param events The input events structure to populate.
/// @param prev The device list from the previous tick.
/// @param curr The device list from the current tick.
internal_function void
OsGenerateGamepadEvents
(
    OS_INPUT_EVENTS *events, 
    OS_GAMEPAD_LIST   *prev,
    OS_GAMEPAD_LIST   *curr
)
{
    OS_INPUT_DEVICE_SET device_set = OS_INPUT_DEVICE_SET_STATIC_INIT;

    // determine the device set, which is used to determine whether devices were added or removed, 
    // and also whether or not (or how) to generate state-based input events for a given device.
    OsDetermineDeviceSet(&device_set, prev, curr);

    // loop through each individual device and emit events.
    events->GamepadAttachCount = 0;
    events->GamepadRemoveCount = 0;
    events->GamepadCount       = 0;
    for (size_t i = 0, n = device_set.DeviceCount; i < n; ++i)
    {
        DWORD   device_id = (DWORD) ((uintptr_t) device_set.DeviceIds[i]);
        switch (device_set.Membership[i])
        {
            case OS_INPUT_DEVICE_MEMBERSHIP_PREV:
                {   // this input device was just removed. no device state events are generated.
                    events->GamepadRemove[events->GamepadRemoveCount] = device_id;
                    events->GamepadRemoveCount++;
                } break;

            case OS_INPUT_DEVICE_MEMBERSHIP_CURR:
                {   // this input device was just attached. no device state events are generated.
                    events->GamepadAttach[events->GamepadAttachCount] = device_id;
                    events->GamepadAttachCount++;
                } break;

            case OS_INPUT_DEVICE_MEMBERSHIP_BOTH:
                {   // this input device was present in both snapshots.
                    OS_GAMEPAD_EVENTS *input_ev = &events->GamepadEvents[events->GamepadCount];
                    OS_GAMEPAD_STATE  *state_pp = &prev->DeviceState[device_set.PrevIndex[i]];
                    OS_GAMEPAD_STATE  *state_cp = &curr->DeviceState[device_set.CurrIndex[i]];
                    events->GamepadIds[events->GamepadCount] = device_id;
                    OsGenerateGamepadInputEvents(input_ev, state_pp, state_cp);
                    events->GamepadCount++;
                } break;
        }
    }
}

/// @summary Copy keyboard devices and state from the current 'current' buffer to the next 'current' buffer.
/// @param dst The 'current' buffer for the next tick.
/// @param src The 'current' buffer for the current tick.
internal_function void
OsForwardKeyboardBuffer
(
    OS_KEYBOARD_LIST *dst, 
    OS_KEYBOARD_LIST *src
)
{   // copy over the device list and key state.
    CopyMemory(dst, src, sizeof(OS_KEYBOARD_LIST));
}

/// @summary Copy pointer devices and state from the current 'current' buffer to the next 'current' buffer.
/// @param dst The 'current' buffer for the next tick.
/// @param src The 'current' buffer for the current tick.
internal_function void
OsForwardPointerBuffer
(
    OS_POINTER_LIST *dst, 
    OS_POINTER_LIST *src
)
{   // copy over the device list, but zero out the relative fields of the state.
    CopyMemory(dst, src, sizeof(OS_POINTER_LIST));
    for (size_t i = 0, n = dst->DeviceCount; i < n; ++i)
    {
        dst->DeviceState[i].Relative[0] = 0;
        dst->DeviceState[i].Relative[1] = 0;
        dst->DeviceState[i].Flags = OS_POINTER_FLAGS_NONE;
    }
}

/// @summary Copy gamepad devices and state from the current 'current' buffer to the next 'current' buffer.
/// @param dst The 'current' buffer for the next tick.
/// @param src The 'current' buffer for the current tick.
internal_function void
OsForwardGamepadBuffer
(
    OS_GAMEPAD_LIST *dst, 
    OS_GAMEPAD_LIST *src
)
{
    UNREFERENCED_PARAMETER(dst);
    UNREFERENCED_PARAMETER(src);
}

/// @summary Push an item onto the private end of a task queue. This function can only be called by the thread that owns the queue, and may execute concurrently with one or more steal operations.
/// @param queue The queue to receive the item.
/// @param task_id The identifier of the task that is ready to run.
/// @return true if the task was written to the queue.
internal_function inline bool
OsTaskQueuePush
(
    OS_TASK_QUEUE *queue, 
    os_task_id_t task_id
)
{
    int64_t b = queue->Private.load(std::memory_order_relaxed);      // atomically load the private end of the queue. only Push and Take may modify the private end.
    queue->TaskIds[b & queue->Mask] = task_id;                       // store the new item at the end of the Tasks array.
    std::atomic_thread_fence(std::memory_order_release);             // ensure that the task ID is written to the Tasks array.
    queue->Private.store(b+1,std::memory_order_relaxed);             // make the new item visible to a concurrent steal/subsequent take operation (push to private end.)
    return true;
}

/// @summary Take an item from the private end of a task queue. This function can only be called by the thread that owns the queue, and may execute concurrently with one or more steal operations.
/// @param queue The queue from which the item will be removed.
/// @param more_items On return, this value is set to true if there was at least one additional item in the queue after the returned item was claimed.
/// @return The task identifier, or OS_INVALID_TASK_ID if the queue is empty.
internal_function os_task_id_t
OsTaskQueueTake
(
    OS_TASK_QUEUE   *queue,
    bool       &more_items
)
{
    int64_t b = queue->Private.load(std::memory_order_relaxed) - 1; // safe since no concurrent Push operation is allowed.
    queue->Private.store(b , std::memory_order_relaxed);            // complete the 'pop' from the private end (LIFO).
    std::atomic_thread_fence(std::memory_order_seq_cst);            // make the 'pop' visible to a concurrent steal.
    int64_t t = queue->Public.load(std::memory_order_relaxed);

    if (t <= b)
    {   // the task queue is non-empty.
        os_task_id_t task_id = queue->TaskIds[b & queue->Mask];
        if (t != b)
        {   // there's at least one more item in the queue; no need to race.
            more_items = true;
            return task_id;
        }
        // this was the last item in the queue. race to claim it.
        if (!queue->Public.compare_exchange_strong(t, t+1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {   // this thread lost the race.
            task_id = OS_INVALID_TASK_ID;
        }
        queue->Private.store(t + 1, std::memory_order_relaxed);
        more_items = false;
        return task_id;
    }
    else
    {   // the queue is currently empty.
        more_items = false;
        queue->Private.store(t, std::memory_order_relaxed);
        return OS_INVALID_TASK_ID;
    }
}

/// @summary Attempt to steal an item from the public end of the queue. This function can be called by any thread EXCEPT the thread that owns the queue, and may execute concurrently with a push or take operation, and one or more steal operations.
/// @param queue The queue from which the item will be removed.
/// @param more_items On return, this value is set to true if there was at least one additional item in the queue after the returned item was claimed.
/// @return The task identifier, or OS_INVALID_TASK_ID if the queue is empty or the calling thread lost the race for the last item.
internal_function os_task_id_t
OsTaskQueueSteal
(
    OS_TASK_QUEUE  *queue, 
    bool      &more_items
)
{
    int64_t t = queue->Public.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    int64_t b = queue->Private.load(std::memory_order_acquire);

    if (t < b)
    {   // the task queue is non-empty. save the task ID.
        os_task_id_t task_id = queue->TaskIds[t & queue->Mask];
        // race with other threads to claim the item.
        if (queue->Public.compare_exchange_strong(t, t+1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {   // the calling thread won the race and claimed the item.
            more_items = (t != b);
            return task_id;
        }
        else
        {   // the calling thread lost the race and should try again.
            more_items = false;
            return OS_INVALID_TASK_ID;
        }
    }
    else
    {   // the queue is currently empty.
        more_items = false;
        return OS_INVALID_TASK_ID;
    }
}

/// @summary Reset a task queue to empty.
/// @param queue The queue to clear.
internal_function inline void
OsTaskQueueClear
(
    OS_TASK_QUEUE *queue
)
{
    queue->Public.store(0, std::memory_order_relaxed);
    queue->Private.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

/// @summary Allocate the memory for a task queue and initialize the queue to empty.
/// @param queue The task queue to initialize.
/// @param capacity The capacity of the queue. This value must be a power of two greater than zero.
/// @param arena The memory arena to allocate from. The caller should ensure that sufficient memory is available.
/// @return Zero if the queue is created successfully, or -1 if an error occurred.
internal_function int
OsCreateTaskQueue
(
    OS_TASK_QUEUE        *queue, 
    size_t             capacity, 
    OS_HOST_MEMORY_ARENA *arena
)
{   // the capacity must be a power of two.
    assert((capacity & (capacity - 1)) == 0);
    queue->Public.store(0, std::memory_order_relaxed);
    queue->Private.store(0, std::memory_order_relaxed);
    queue->Mask = int64_t(capacity) - 1;
    queue->TaskIds = (os_task_id_t*) OsHostMemoryArenaAllocate(arena, capacity * sizeof(os_task_id_t), std::alignment_of<os_task_id_t>::value);
    return queue->TaskIds != NULL ? 0 : -1;
}

/// @summary Send an application-defined signal from one worker thread to one or more other worker threads in the same pool.
/// @param iocp The I/O completion port handle for the thread pool.
/// @param signal_arg The application-defined data to send as the signal.
/// @param thread_count The number of waiting threads to signal.
/// @param last_error If the function returns false, the system error code is stored in this location.
/// @return true if the specified number of signal notifications were successfully posted to the thread pool.
internal_function bool
OsSignalWorkerThreads
(
    HANDLE          iocp, 
    uintptr_t signal_arg, 
    size_t  thread_count, 
    DWORD    &last_error
)
{
    for (size_t i = 0; i < thread_count; ++i)
    {
        if (!PostQueuedCompletionStatus(iocp, 0, signal_arg, NULL))
        {   // only report the first failure.
            if (last_error == ERROR_SUCCESS)
            {   // save the error code.
                last_error  = GetLastError();
            } return false;
        }
    }
    return true;
}

/// @summary Call from a thread pool worker only. Puts the worker thread to sleep until a signal is received on the pool, or the pool is shutting down.
/// @param iocp The handle of the I/O completion port used to signal the thread pool.
/// @param term The handle of the manual-reset event used to signal worker threads to terminate.
/// @param tid The operating system thread identifier of the calling worker thread.
/// @param key The location to receive the pointer value sent with the wake signal.
/// @return One of OS_WORKER_THREAD_WAKE_REASON.
internal_function int
OsWorkerThreadWaitForWakeup
(
    HANDLE    iocp,
    HANDLE    term, 
    uint32_t   tid,
    uintptr_t &key
)
{
    OVERLAPPED *ov_addr = NULL;
    DWORD        nbytes = 0;
    DWORD        termrc = 0;

    // prior to entering the wait state, check the termination signal.
    if ((termrc = WaitForSingleObject(term, 0)) != WAIT_TIMEOUT)
    {   // either termination was signaled, or an error occurred.
        // either way, exit prior to entering the wait state.
        if (termrc != WAIT_OBJECT_0)
        {   // an error occurred while checking the termination signal.
            OsLayerError("ERROR: %S(%u): Checking termination signal failed with result 0x%08X (0x%08X).\n", __FUNCTION__, tid, termrc, GetLastError());
        }
        return OS_WORKER_THREAD_WAKE_FOR_EXIT;
    }
    if (GetQueuedCompletionStatus(iocp, &nbytes, &key, &ov_addr, INFINITE))
    {   // check the termination signal before possibly dispatching work.
        int      rc = key != 0 ? OS_WORKER_THREAD_WAKE_FOR_RUN : OS_WORKER_THREAD_WAKE_FOR_SIGNAL;
        if ((termrc = WaitForSingleObject(term, 0)) != WAIT_TIMEOUT)
        {
            if (termrc != WAIT_OBJECT_0)
            {   // an error occurred while checking the termination signal.
                OsLayerError("ERROR: %S(%u): Checking termination signal failed with result 0x%08X (0x%08X).\n", __FUNCTION__, tid, termrc, GetLastError());
            }
            rc = OS_WORKER_THREAD_WAKE_FOR_EXIT;
        }
        return rc;
    }
    else
    {   // the call to GetQueuedCompletionStatus failed.
        OsLayerError("ERROR: %S(%u): GetQueuedCompletionStatus failed with result 0x%08X.\n", __FUNCTION__, tid, GetLastError());
        return OS_WORKER_THREAD_WAKE_FOR_ERROR;
    }
}

/// @summary Convert a lowercase ASCII character 'a'-'z' to the corresponding uppercase character.
/// @param ch The input character.
/// @return The uppercase version of the input character.
internal_function inline int
OsToUpper
(
    char ch
)
{   // TODO(rlk): this works only for C locale. if locale support is needed, use toupper in ctype.h.
    return (ch >= 'a' && ch <= 'z') ? (ch - 'a' + 'A') : ch;
}

/// @summary Convert a lowercase ASCII character 'a'-'z' to the corresponding uppercase character.
/// @param ch The input character.
/// @return The uppercase version of the input character.
internal_function inline int
OsToUpper
(
    WCHAR ch
)
{   // TODO(rlk): this works only for C locale. if locale support is needed, use towupper in ctype.h.
    return (ch >= L'a' && ch <= L'z') ? (ch - L'a' + L'A') : ch;
}

/// @summary Convert an uppercase ASCII character 'A'-'Z' to the corresponding lowercase character.
/// @param ch The input character.
/// @return The lowercase version of the input character.
internal_function inline int
OsToLower
(
    char ch
)
{   // TODO(rlk): this works only for C locale. if locale support is needed, use tolower in ctype.h.
    return (ch >= 'A' && ch <= 'Z') ? (ch - 'A' + 'a') : ch;
}

/// @summary Convert an uppercase ASCII character 'A'-'Z' to the corresponding lowercase character.
/// @param ch The input character.
/// @return The lowercase version of the input character.
internal_function inline int
OsToLower
(
    WCHAR ch
)
{   // TODO(rlk): this works only for C locale. if locale support is needed, use towlower in ctype.h.
    return (ch >= L'A' && ch <= L'Z') ? (ch - L'A' + L'a') : ch;
}

/// @summary Normalize path separators for a character.
/// @param ch The character to inspect.
/// @return The forward slash if ch is a backslash, otherwise ch.
internal_function inline uint32_t 
OsNormalizePathSeparator
(
    char ch
)
{
    return ch != '\\' ? uint32_t(OsToLower(ch)) : uint32_t('/');
}

/// @summary Normalize path separators for a character.
/// @param ch The character to inspect.
/// @return The forward slash if ch is a backslash, otherwise ch.
internal_function inline uint32_t
OsNormalizePathSeparator
(
    WCHAR ch
)
{
    return ch != L'\\' ? uint32_t(OsToLower(ch)) : uint32_t(L'/');
}

/// @summary Calculates a 32-bit hash value for a path string. Forward and backslashes are treated as equivalent.
/// @param path A NULL-terminated UTF-8 path string.
/// @return The hash of the specified string.
internal_function uint32_t 
OsHashPath
(
    char const *path
)
{
    if (path != NULL && path[0] != 0)
    {
        uint32_t    code = 0;
        uint32_t    hash = 0;
        char const *iter = path;
        do {
            code = OsNormalizePathSeparator(*iter++);
            hash = _lrotl(hash, 7) + code;
        } while (code != 0);
        return hash;
    }
    else return 0;
}

/// @summary Calculates a 32-bit hash value for a path string. Forward and backslashes are treated as equivalent.
/// @param path A NULL-terminated UTF-8 path string.
/// @return The hash of the specified string.
internal_function uint32_t
OsHashPath
(
    WCHAR const *path
)
{
    if (path != NULL && path[0] != 0)
    {
        uint32_t     code = 0;
        uint32_t     hash = 0;
        WCHAR const *iter = path;
        do {
            code = OsNormalizePathSeparator(*iter++);
            hash = _lrotl(hash, 7) + code;
        } while (code != 0);
        return hash;
    }
    else return 0;
}

/// @summary Locate one substring within another, ignoring case.
/// @param search The zero-terminated ASCII string to search.
/// @param find The zero-terminated ASCII string to locate within the search string.
/// @return A pointer to the start of the first match, or NULL.
internal_function char const*
OsStringSearch
(
    char const *search, 
    char const *find
)
{
    if (search == NULL)
    {   // invalid search string.
        return NULL;
    }
    if (*search == 0 && *find == 0)
    {   // both are empty strings.
        return search;
    }
    else if (*search == 0)
    {   // search is an empty string, but find is not.
        return NULL;
    }
    else
    {   // both search and find are non-NULL, non-empty strings.
        char const *si = search;
        int  const  ff = OsToLower(*find++);
        for ( ; ; )
        {
            int const sc = OsToLower(*si++);
            if (sc == ff)
            {   // first character of find matches this character of search.
                char const *ss = si;   // si points to one after the match.
                char const *fi = find; // find points to the second character.
                for ( ; ; )
                {
                    int const s = OsToLower(*ss++);
                    int const f = OsToLower(*fi++);
                    if (f == 0) return si-1;
                    if (s == 0) return NULL;
                    if (s != f) break;
                }
            }
            if (sc == 0) break;
        }
    }
    return NULL;
}

/// @summary Figure out the starting and ending points of the directory path, filename and extension information in a path string.
/// @param buf The zero-terminated path string to parse. Any forward slashes are converted to backslashes.
/// @param buf_end A pointer to the zero terminator character of the input path string. This value must be valid.
/// @param parts The OS_PATH_PARTS to update. The Root, RootEnd and PathFlags fields must be set.
/// @return This function always returns zero to indicate success.
internal_function int
OsExtractNativePathParts
(
    WCHAR           *buf, 
    WCHAR       *buf_end, 
    OS_PATH_PARTS *parts
)
{   // initialize the components of the path parts to known values.
    parts->Path         = parts->RootEnd;
    parts->PathEnd      = parts->RootEnd;
    parts->Filename     = parts->RootEnd;
    parts->FilenameEnd  = parts->RootEnd;
    parts->Extension    = buf_end;
    parts->ExtensionEnd = buf_end;

    while (parts->FilenameEnd < buf_end)
    {
        if (parts->FilenameEnd[0] == L'/')
            parts->FilenameEnd[0]  = L'\\';
        if (parts->FilenameEnd[0] == L'\\')
        {   // encountered a path separator.
            // update the end of the directory path string.
            // reset the filename string to be zero-length.
            parts->PathEnd     = parts->FilenameEnd;
            parts->PathFlags  |= OS_PATH_FLAG_PATH;
            parts->Filename    = parts->FilenameEnd + 1;
            parts->FilenameEnd = parts->FilenameEnd + 1;
        }
        else
        {   // this is a regular character; consider it part of the filename.
            parts->FilenameEnd++;
        }
    }
    if (parts->Path[0] == L'\\')
    {   // skip the leading path separator.
        if (parts->Path == parts->PathEnd)
        {   // there is no actual path component; this is something like "C:\".
            parts->PathFlags &= ~OS_PATH_FLAG_PATH;
            parts->PathEnd++;
        }
        parts->Path++;
    }

    if (parts->Filename != parts->FilenameEnd)
    {   // figure out whether this last bit is a filename or part of the directory path.
        WCHAR *iter  = buf_end;
        while (iter >= parts->Filename)
        {   // consider 'a.b', '.a.b' and 'a.' to be a filename, but not '.a'.
            if (*iter == L'.' && iter != parts->Filename)
            {
                parts->FilenameEnd = iter;
                parts->Extension   = iter + 1;
                parts->PathFlags  |= OS_PATH_FLAG_FILENAME;
                parts->PathFlags  |= OS_PATH_FLAG_EXTENSION;
            }
            iter--;
        }
        if ((parts->PathFlags & OS_PATH_FLAG_FILENAME) == 0)
        {   // consider 'a' and '.a' to be part of the directory path information.
            parts->PathEnd     = parts->FilenameEnd;
            parts->PathFlags  |= OS_PATH_FLAG_PATH;
            parts->Filename    = buf_end;
            parts->FilenameEnd = buf_end;
        }
    }
    else
    {   // if there's no filename present, make sure that it points to an empty string.
        parts->Filename    = buf_end;
        parts->FilenameEnd = buf_end;
    }
    UNREFERENCED_PARAMETER(buf);
    return 0;
}

/// @summary Enumerate all files in a directory (and possibly its subdirectories.)
/// @param buf The native path to search. The buffer contents are overwritten during enumeration.
/// @param buf_bytes The maximum number of bytes that can be written to the path string buffer.
/// @param buf_end A pointer to the zero-terminator codepoint within the path string buffer.
/// @param filter A zero-terminated string specifying the filter to use when locating files. The filter supports wildcards. Specify a filter of * to include all files in the search.
/// @param recurse Specify true to include files located in subdirectories in the search results.
/// @param total_files This value is updated to include the total number of files returned by the search.
/// @param alloc The OS_FILE_INFO_CHUNK allocator to use for allocating blocks of file information to store the search results.
/// @param chunk The OS_FILE_INFO_CHUNK to which search results will be written, or NULL if no search results have been returned yet. New chunks are allocated as-needed.
/// @return A pointer to the OS_FILE_INFO_CHUNK containing the search results.
internal_function OS_FILE_INFO_CHUNK*
OsEnumerateDirectory
(
    WCHAR                *buf, 
    size_t          buf_bytes, 
    WCHAR            *buf_end,
    WCHAR const       *filter, 
    bool              recurse, 
    size_t       &total_files, 
    OS_FSIC_ALLOCATOR  *alloc, 
    OS_FILE_INFO_CHUNK *chunk
)
{   size_t const MAX_UTF8 = 1024; // maximum of 1024 bytes of UTF-8 data returned for filename.
    HANDLE    find_handle = INVALID_HANDLE_VALUE;
    WCHAR       *base_end = buf_end;
    WCHAR     *filter_end = buf_end;
    size_t   filter_chars = 0;
    size_t   bytes_needed = 0;
    int       fname_bytes = 0;
    WIN32_FIND_DATA    fd;
    char utf8buf[MAX_UTF8];

    if (recurse)
    {   // if recursion is enabled, filter with *, and recurse into all subdirectories.
        if ((filter_chars = OsNativePathAppend(buf, buf_bytes, &filter_end, bytes_needed, L"*")) == 0)
        {
            OsLayerError("ERROR: %S(%u): Unable to construct * filter string for path %s.\n", __FUNCTION__, GetCurrentThreadId(), buf);
            return chunk;
        }
        if ((find_handle = FindFirstFileEx(buf, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH)) == INVALID_HANDLE_VALUE)
        {
            OsLayerError("ERROR: %S(%u): Search filter %s failed (%08X).\n", __FUNCTION__, GetCurrentThreadId(), buf, GetLastError());
            return chunk;
        }
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if ((fd.cFileName[0] == L'.' && fd.cFileName[1] == 0) || (fd.cFileName[0] == L'.' && fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))
                {   // skip the current directory and parent directory links.
                    continue;
                }
                else
                {   // build the path string for the subdirectory.
                    *base_end = 0; filter_end = base_end;
                    if ((filter_chars = OsNativePathAppend(buf, buf_bytes, &filter_end, bytes_needed, fd.cFileName)) == 0)
                    {
                        *base_end = 0; filter_end = base_end;
                        OsLayerError("ERROR: %S(%u): Unable to recurse into subdirectory %s of path %s.\n", __FUNCTION__, GetCurrentThreadId(), fd.cFileName, buf);
                    }
                    // recurse into the subdirectory.
                    chunk = OsEnumerateDirectory(buf, buf_bytes, filter_end, filter, recurse, total_files, alloc, chunk);
                }
            }
        } 
        while (FindNextFile(find_handle, &fd));
        FindClose(find_handle); find_handle = INVALID_HANDLE_VALUE;
    }

    // now search with the actual filter applied, and look at files only.
    *base_end = 0; filter_end = base_end;
    if ((filter_chars = OsNativePathAppend(buf, buf_bytes, &filter_end, bytes_needed, filter)) == 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to construct filter string from %s and %s.\n", __FUNCTION__, GetCurrentThreadId(), buf, filter);
        return chunk;
    }
    if ((find_handle = FindFirstFileEx(buf, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH)) == INVALID_HANDLE_VALUE)
    {
        OsLayerError("ERROR: %S(%u): Search filter %s failed (%08X).\n", __FUNCTION__, GetCurrentThreadId(), buf, GetLastError());
        return chunk;
    }
    do
    {   // skip over anything that's not a proper file or symlink.
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DEVICE)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_VIRTUAL)
            continue;

        // build the absolute path of the file.
        *base_end = 0; filter_end = base_end;
        if ((filter_chars = OsNativePathAppend(buf, buf_bytes, &filter_end, bytes_needed, fd.cFileName)) == 0)
        {
            OsLayerError("ERROR: %S(%u): Failed to build filename for path %s, file %s.\n", __FUNCTION__, GetCurrentThreadId(), buf, fd.cFileName);
            continue;
        }
        if (chunk == NULL || chunk->RecordCount == OS_FILE_INFO_CHUNK::CAPACITY)
        {   // allocate a new chunk; the current chunk is full.
            OS_FILE_INFO_CHUNK *new_chunk = OsNewFileInfoChunk(alloc);
            if (new_chunk == NULL)
            {
                OsLayerError("ERROR: %S(%u): Failed to allocate OS_FILE_INFO_CHUNK for file %s.\n", __FUNCTION__, GetCurrentThreadId(), buf);
                continue;
            }
            // entries higher up in the tree appear first.
            new_chunk->NextChunk = chunk;
            chunk = new_chunk;
        }
        // fill out the information for the file in the current chunk.
        chunk->PathHash[chunk->RecordCount]            = OsHashPath(buf);
        chunk->FileInfo[chunk->RecordCount].FileSize   = OsMakeInt64(fd.nFileSizeHigh, fd.nFileSizeLow);
        chunk->FileInfo[chunk->RecordCount].BaseOffset = 0;
        chunk->FileInfo[chunk->RecordCount].LastWrite  = fd.ftLastWriteTime;
        chunk->FileInfo[chunk->RecordCount].Attributes = fd.dwFileAttributes;
        // convert the filename from file system native to UTF-8.
        // this data will be used for collision resolution and extension searching.
        if ((fname_bytes = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, utf8buf, MAX_UTF8, NULL, NULL)) == 0)
        {
            OsLayerError("ERROR: %S(%u): Failed to convert filename %s to UTF-8 (%08X).\n", __FUNCTION__, GetCurrentThreadId(), fd.cFileName, GetLastError());
            continue;
        }
        // copy as much data as possible to the filename buffer.
        // fname_bytes includes one byte for the zero-terminator.
        size_t fnmax = OS_FILE_INFO::MAX_STRING_BYTES - 1;
        char  *fndst = chunk->FileInfo[chunk->RecordCount].FileName;
        char  *fnend = utf8buf + fname_bytes;
        char  *fnsrc = utf8buf;
        char  *fnext = NULL;
        if (fnmax >= size_t(fname_bytes - 1))
            fnmax  = size_t(fname_bytes - 1);
        CopyMemory(fndst, fnsrc, fnmax);
        fndst[fnmax] = 0;
        // find the file extension and copy as much as possible into the extension buffer.
        // note that the first character is intentionally NOT included.
        while (fnend > fnsrc)
        {
            if(*fnend == '.')
                fnext = fnend;
            fnend--;
        }
        if (fnext != NULL)
        {   // copy the file extension data.
            fndst  = chunk->FileInfo[chunk->RecordCount].Extension;
            fnmax  = OS_FILE_INFO::MAX_STRING_BYTES - 1;
            fnend  = utf8buf + fname_bytes;
            if (fnmax >= size_t(fnend - fnext - 1))
                fnmax  = size_t(fnend - fnext - 1);
            CopyMemory(fndst, fnext, fnmax);
            fndst[fnmax] = 0;
        }
        else
        {   // there is no file extension.
            chunk->FileInfo[chunk->RecordCount].Extension[0] = 0;
        }
        // all data has been written to the new record. make it visible.
        chunk->RecordCount++;
        total_files++;
    } 
    while (FindNextFile(find_handle, &fd));

    // finished searching; clean up and return.
    FindClose(find_handle); find_handle = INVALID_HANDLE_VALUE;
    return chunk;
}

/// @summary Helper function to convert a UTF-8 encoded string to the system native WCHAR. Free the returned buffer using the standard C library free() call.
/// @param str The NULL-terminated UTF-8 string to convert.
/// @param size_chars On return, stores the length of the string in characters, not including NULL-terminator.
/// @param size_bytes On return, stores the length of the string in bytes, including the NULL-terminator.
/// @return The WCHAR string buffer, or NULL if the string could not be converted.
internal_function WCHAR*
OsUtf8ToNativeMalloc
(
    char const    *str, 
    size_t &size_chars, 
    size_t &size_bytes
)
{   // figure out how much memory needs to be allocated, including NULL terminator.
    int nchars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, NULL, 0);
    if (nchars == 0)
    {   // the path cannot be converted from UTF-8 to UCS-2.
        size_chars = 0;
        size_bytes = 0;
        return NULL;
    }
    // store output values for the caller.
    size_chars = nchars - 1;
    size_bytes = nchars * sizeof(WCHAR);
    // allocate buffer space for the wide character string.
    WCHAR *pathbuf = NULL;
    if   ((pathbuf = (WCHAR*) malloc(size_bytes)) == NULL)
    {   // unable to allocate temporary memory for UCS-2 path.
        OsLayerError("ERROR: %S(%u): Memory allocation for %Iu bytes failed.\n", __FUNCTION__, GetCurrentThreadId(), size_bytes);
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, pathbuf, nchars) == 0)
    {   // the path cannot be converted from UTF-8 to UCS-2.
        OsLayerError("ERROR: %S(%u): Cannot convert from UTF8 to UCS2 (%08X).\n", __FUNCTION__, GetCurrentThreadId(), GetLastError());
        free(pathbuf);
        return NULL;
    }
    return pathbuf;
}

/// @summary Generate a Windows API-ready file path for a given virtual path specified relative to the mount point root.
/// @param mount_local_path A zero-terminated buffer specifying the filesystem mount point local path.
/// @param local_path_length The number of characters in the mount_local_path string, not including the zero terminator.
/// @param relative_path The portion of the virtual path specified relative to the mount point root.
/// @return The API-ready system path, or NULL. Free the returned buffer using free().
internal_function WCHAR* 
OsMakeSystemPathMalloc
(
    WCHAR        *mount_local_path, 
    size_t const local_path_length,
    char   const    *relative_path
)
{
    WCHAR  *pathbuf = NULL;
    int     nchars  = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, relative_path, -1, NULL, 0);
    // convert the virtual_path from UTF-8 to UCS-2, which Windows requires.
    if (nchars == 0)
    {   // the path cannot be converted from UTF-8 to UCS-2.
        return NULL;
    }
    if ((pathbuf = (WCHAR*) malloc((nchars + local_path_length + 1) * sizeof(WCHAR))) == NULL)
    {   // unable to allocate memory for UCS-2 path.
        return NULL;
    }
    // start with the local root, and then append the relative path portion.
    memcpy(pathbuf, mount_local_path, local_path_length * sizeof(WCHAR));
    pathbuf[local_path_length] = L'\\';
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, relative_path, -1, &pathbuf[local_path_length+1], nchars) == 0)
    {   // the path cannot be converted from UTF-8 to UCS-2.
        free(pathbuf);
        return NULL;
    }
    // normalize the directory separator characters to the system default.
    // on Windows, this is not strictly necessary, but on other OS's it is.
    for (size_t i = local_path_length + 1, n = local_path_length + nchars; i < n; ++i)
    {
        if (pathbuf[i] == L'/')
            pathbuf[i]  = L'\\';
    }
    return pathbuf;
}

/// @summary Retrieve the fully-resolved absolute path for a file or directory.
/// @param handle The handle of the opened file or directory.
/// @param length On return, the number of characters in the returned path string is stored here, not including the zero terminator.
/// @return The zero-terminated, fully-resolved absolute path of the file system entity, or NULL. Free the returned buffer using free().
internal_function WCHAR*
OsResolvePathForHandleMalloc
(
    HANDLE  handle, 
    size_t &length
)
{
    WCHAR *pathbuf = NULL;
    DWORD    flags = VOLUME_NAME_DOS | FILE_NAME_NORMALIZED;
    DWORD  pathlen = GetFinalPathNameByHandleW(handle, NULL, 0, flags);
    // GetFinalPathNameByHandle returns the buffer length, in TCHARs, including zero terminator.
    if (pathlen == 0)
    {
        length  = 0;
        return NULL;
    }
    if ((pathbuf = (WCHAR*) malloc(pathlen * sizeof(WCHAR))) == NULL)
    {
        length  = 0;
        return NULL;
    }
    // GetFinalPathNameByHandle returns the number of TCHARs written, not including zero terminator.
    if (GetFinalPathNameByHandleW(handle, pathbuf, pathlen-1, flags) != (pathlen-1))
    {
        free(pathbuf);
        length  = 0;
        return NULL;
    }
    pathbuf[pathlen-1] = 0;
    length = pathlen-1;
    return pathbuf;
}

/// @summary Execute a low-level file open operation.
/// @param args Information about the file to open. The FilePath and IoHintFlags fields must be set.
/// @param result Information returned from the operation specifying the file handle and whether the operation completed successfully.
/// @param file_info If this argument is not NULL, information about the file is stored here on return.
/// @param sector_size If this argument is not NULL, the physical device sector size is stored here on return.
internal_function void
OsIoExecuteOpen
(
    OS_IO_OPERATION          &args, 
    OS_IO_OPERATION_RESULT &result, 
    FILE_STANDARD_INFO  *file_info, 
    size_t            *sector_size
)
{
    HANDLE    fd = INVALID_HANDLE_VALUE;
    DWORD access = 0; // dwDesiredAccess
    DWORD share  = 0; // dwShareMode
    DWORD create = 0; // dwCreationDisposition
    DWORD flags  = 0; // dwFlagsAndAttributes

    if (args.IoHintFlags & OS_IO_HINT_FLAG_OVERWRITE)
    {   // this implies write access.
        args.IoHintFlags |= OS_IO_HINT_FLAG_WRITE;
    }
    if (args.IoHintFlags & OS_IO_HINT_FLAG_READ)
    {
        access |= GENERIC_READ;
        share   = FILE_SHARE_READ;
        create  = OPEN_EXISTING;
        flags   = FILE_FLAG_OVERLAPPED;
    }
    if (args.IoHintFlags & OS_IO_HINT_FLAG_WRITE)
    {
        access |= GENERIC_WRITE;
        flags   = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
        if (args.IoHintFlags & OS_IO_HINT_FLAG_OVERWRITE)
        {   // opening the file will always succeed.
            create = CREATE_ALWAYS;
        }
        else
        {   // opening the file will always succeed, but existing contents are preserved.
            create = OPEN_ALWAYS;
        }
        if (args.IoHintFlags & OS_IO_HINT_FLAG_TEMPORARY)
        {   // temporary files are deleted on close, and the cache manager will try to prevent writes to disk.
            flags |= FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE;
            share |= FILE_SHARE_DELETE;
        }
        else
        {   // standard persistent file, data will eventually end up on disk.
            flags |= FILE_ATTRIBUTE_NORMAL;
        }
    }
    if (args.IoHintFlags & OS_IO_HINT_FLAG_SEQUENTIAL)
    {   // tell the cache manager to optimize for sequential access.
        flags |= FILE_FLAG_SEQUENTIAL_SCAN;
    }
    else
    {   // assume the file will be accessed randomly.
        flags |= FILE_FLAG_RANDOM_ACCESS;
    }
    if (args.IoHintFlags & OS_IO_HINT_FLAG_UNCACHED)
    {   // use unbuffered I/O, reads must be performed in sector size multiples to 
        // a buffer whose address is also a multiple of the physical disk sector size.
        flags |= FILE_FLAG_NO_BUFFERING;
    }
    if (args.IoHintFlags & OS_IO_HINT_FLAG_WRITE_THROUGH)
    {   // writes are immediately flushed to disk, if possible.
        flags |= FILE_FLAG_WRITE_THROUGH;
    }
    if ((fd = CreateFile(args.FilePath, access, share, NULL, create, flags, NULL)) == INVALID_HANDLE_VALUE)
    {
        OsLayerError("ERROR: %S(%u): Failed to open file \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), args.FilePath, GetLastError());
        result.FileHandle = INVALID_HANDLE_VALUE;
        result.ResultCode = GetLastError();
        result.TransferAmount = 0;
        result.CompletedSynchronously = true;
        result.WasSuccessful = false;
        return;
    }
    if (args.CompletionPort != NULL && CreateIoCompletionPort(fd, args.CompletionPort, 0, 0) != args.CompletionPort)
    {
        OsLayerError("ERROR: %S(%u): Unable to associate file \"%s\" with I/O completion port (%08X).\n", __FUNCTION__, GetCurrentThreadId(), args.FilePath, GetLastError());
        result.FileHandle = INVALID_HANDLE_VALUE;
        result.ResultCode = GetLastError();
        result.TransferAmount = 0;
        result.CompletedSynchronously = true;
        result.WasSuccessful = false;
        CloseHandle(fd);
        return;
    }
    if (args.CompletionPort != NULL)
    {   // immediately complete requests that execute synchronously; don't post completion port notification.
        // this reduces traffic on the completion port and improves performance for asynchronous requests.
        SetFileCompletionNotificationModes(fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
    }
    if ((args.IoHintFlags & OS_IO_HINT_FLAG_PREALLOCATE) && args.PreallocationSize > 0)
    {   // preallocate storage space for the file data, which can significantly improve performance when writing large files.
        // this can be slower for large files if the file is not written sequentially.
        LARGE_INTEGER new_ptr; new_ptr.QuadPart = args.PreallocationSize;
        if (SetFilePointerEx(fd, new_ptr, NULL, FILE_BEGIN))
        {   // set the end-of-file marker to the new location.
            if (SetEndOfFile(fd))
            {   // move the file pointer back to the start of the file.
                new_ptr.QuadPart = 0;
                SetFilePointerEx(fd, new_ptr, NULL, FILE_BEGIN);
            }
            else
            {   // this is a non-fatal error since preallocation is an optimization.
                OsLayerError("ERROR: %S(%u): Failed to preallocate file \"%s\" to %I64d bytes. SetEndOfFile failed (%08X).\n", __FUNCTION__, GetCurrentThreadId(), args.FilePath, args.PreallocationSize, GetLastError()); 
            }
        }
        else
        {   // this is a non-fatal error since preallocation is an optimization.
            OsLayerError("ERROR: %S(%u): Failed to preallocate file \"%s\" to %I64d bytes. SetFilePointerEx failed (%08X).\n", __FUNCTION__, GetCurrentThreadId(), args.FilePath, args.PreallocationSize, GetLastError()); 
        }
    }
    if (file_info != NULL)
    {   // retrieve the end-of-file (logical file size) and allocation size (physical size).
        ZeroMemory(file_info, sizeof(FILE_STANDARD_INFO));
        if (!GetFileInformationByHandleEx(fd, FileStandardInfo, file_info, sizeof(FILE_STANDARD_INFO)))
        {   // this is a non-fatal error since it's just an information request.
            OsLayerError("ERROR: %S(%u): Failed to retrieve attributes for file \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), args.FilePath, GetLastError());
        }
    }
    if (sector_size != NULL)
    {   // retrieve the physical device sector size. this value is necessary for unbuffered I/O operations, 
        // where the buffer address and transfer size must be a multiple of the physical device sector size.
        // TODO(rlk): for this to work, it needs the DEVICE path, ie. \\.\C:, and then you have to open a handle.
        // TODO(rlk): GetFileInformationByHandleEx could also be used, but that is Windows 8+ for the property we need.
        // TODO(rlk): for now, just hard-code to 4096.
        /*STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR desc;
        STORAGE_PROPERTY_QUERY query;
        DWORD bytes = 0;
        ZeroMemory(&desc , sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR));
        ZeroMemory(&query, sizeof(STORAGE_PROPERTY_QUERY));
        query.QueryType  = PropertyStandardQuery;
        query.PropertyId = StorageAccessAlignmentProperty;
        if (DeviceIoControl(fd, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &desc, sizeof(desc), &bytes, NULL))
        {   // save the physical device sector size.
           *sector_size  = desc.BytesPerPhysicalSector; 
        }
        else
        {   // this is a non-fatal error; return a default that will work for all common disks.
            OsLayerError("ERROR: %S(%u): Failed to retrieve physical device sector size for file \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), args.FilePath, GetLastError());
           *sector_size = 4096;
        }*/
        *sector_size = 4096;
    }
    result.FileHandle = fd;
    result.ResultCode = GetLastError();
    result.TransferAmount = 0;
    result.CompletedSynchronously = true;
    result.WasSuccessful = true;
}

/// @summary Execute a synchronous or asynchronous read operation.
/// @param args Information about the read operation. The FileHandle, DataBuffer and TransferAmount fields must be set.
/// @param result Information returned from the operation specifying the number of bytes transferred and whether the operation completed successfully.
internal_function void
OsIoExecuteRead
(
    OS_IO_OPERATION          &args,
    OS_IO_OPERATION_RESULT &result
)
{   
    DWORD transferred = 0;
    // the request is submitted to be performed asynchronously, but can complete
    // either synchronously or asynchronously. this routine hides that complexity.
    if (args.Overlapped)
    {   // populate the required fields of the OVERLAPPED strucure for an asynchronous request.
        args.Overlapped->Internal     = 0;
        args.Overlapped->InternalHigh = 0;
        args.Overlapped->Offset       =(DWORD) (args.FileOffset        & 0xFFFFFFFFUL);
        args.Overlapped->OffsetHigh   =(DWORD)((args.FileOffset >> 32) & 0xFFFFFFFFUL);
    }
    else if (args.FileOffset >= 0)
    {   // for synchronous I/O, support implicit seeking within the file.
        LARGE_INTEGER new_ptr; new_ptr.QuadPart = args.FileOffset;
        if (!SetFilePointerEx(args.FileHandle, new_ptr, NULL, FILE_BEGIN))
        {   // the seek operation failed - fail the read operation.
            result.FileHandle = args.FileHandle;
            result.ResultCode = GetLastError();
            result.CompletedSynchronously = true;
            result.WasSuccessful = false;
            return;
        }
    }
    if (ReadFile(args.FileHandle, args.DataBuffer, args.TransferAmount, &transferred, args.Overlapped))
    {   // the read operation completed synchronously (likely the data was in-cache.)
        result.FileHandle = args.FileHandle;
        result.ResultCode = GetLastError();
        result.TransferAmount = transferred;
        result.CompletedSynchronously = true;
        result.WasSuccessful = true;
    }
    else
    {   // the operation could have failed, or it could be completing asynchronously.
        // it could also be the case that end-of-file was reached.
        switch ((result.ResultCode = GetLastError()))
        {
            case ERROR_IO_PENDING:
                { // the request will complete asynchronously.
                  result.FileHandle = args.FileHandle;
                  result.TransferAmount = 0;
                  result.CompletedSynchronously = false;
                  result.WasSuccessful = true;
                } break;
            case ERROR_HANDLE_EOF:
                { // attempt to read past end-of-file; result.TransferAmount is set to the number of bytes available.
                  result.FileHandle = args.FileHandle;
                  result.TransferAmount = transferred;
                  result.CompletedSynchronously = true;
                  result.WasSuccessful = true;
                } break;
            default:
                { // an actual error occurred.
                  result.FileHandle = args.FileHandle;
                  result.TransferAmount = transferred;
                  result.CompletedSynchronously = true;
                  result.WasSuccessful = false;
                } break;
        }
    }
}

/// @summary Execute a synchronous or asynchronous write operation.
/// @param args Information about the write operation. The FileHandle, DataBuffer and TransferAmount fields must be set.
/// @param result Information returned from the operation specifying the number of bytes transferred and whether the operation completed successfully.
internal_function void
OsIoExecuteWrite
(
    OS_IO_OPERATION          &args, 
    OS_IO_OPERATION_RESULT &result
)
{   
    DWORD transferred = 0;
    // the request is submitted to be performed asynchronously, but can complete
    // either synchronously or asynchronously. this routine hides that complexity.
    if (args.Overlapped)
    {   // populate the required fields of the OVERLAPPED strucure for an asynchronous request.
        args.Overlapped->Internal     = 0;
        args.Overlapped->InternalHigh = 0;
        args.Overlapped->Offset       =(DWORD) (args.FileOffset        & 0xFFFFFFFFUL);
        args.Overlapped->OffsetHigh   =(DWORD)((args.FileOffset >> 32) & 0xFFFFFFFFUL);
    }
    else if (args.FileOffset >= 0)
    {   // for synchronous I/O, support implicit seeking within the file.
        LARGE_INTEGER new_ptr; new_ptr.QuadPart = args.FileOffset;
        if (!SetFilePointerEx(args.FileHandle, new_ptr, NULL, FILE_BEGIN))
        {   // the seek operation failed - fail the write operation.
            result.FileHandle = args.FileHandle;
            result.ResultCode = GetLastError();
            result.CompletedSynchronously = true;
            result.WasSuccessful = false;
            return;
        }
    }
    if (WriteFile(args.FileHandle, args.DataBuffer, args.TransferAmount, &transferred, args.Overlapped))
    {   // the write operation completed synchronously.
        result.FileHandle = args.FileHandle;
        result.ResultCode = GetLastError();
        result.TransferAmount = transferred;
        result.CompletedSynchronously = true;
        result.WasSuccessful = true;
    }
    else
    {   // the operation could have failed, or it could be completing asynchronously.
        // it could also be the case that end-of-file was reached.
        switch ((result.ResultCode = GetLastError()))
        {
            case ERROR_IO_PENDING:
                { // the request will complete asynchronously.
                  result.FileHandle = args.FileHandle;
                  result.TransferAmount = 0;
                  result.CompletedSynchronously = false;
                  result.WasSuccessful = true;
                } break;
            default:
                { // an actual error occurred.
                  result.FileHandle = args.FileHandle;
                  result.TransferAmount = transferred;
                  result.CompletedSynchronously = true;
                  result.WasSuccessful = false;
                } break;
        }
    }
}

/// @summary Execute a synchronous flush operation.
/// @param args Information about the flush operation. The FileHandle field must be set.
/// @param result Information returned from the operation specifying whether the operation completed successfully.
internal_function void
OsIoExecuteFlush
(
    OS_IO_OPERATION          &args, 
    OS_IO_OPERATION_RESULT &result
)
{
    if (FlushFileBuffers(args.FileHandle))
    {
        result.FileHandle = args.FileHandle;
        result.ResultCode = GetLastError();
        result.TransferAmount = 0;
        result.CompletedSynchronously = true;
        result.WasSuccessful = true;
    }
    else
    {
        result.FileHandle = args.FileHandle;
        result.ResultCode = GetLastError();
        result.TransferAmount = 0;
        result.CompletedSynchronously = true;
        result.WasSuccessful = false;
    }
}

/// @summary Execute a synchronous file close operation.
/// @param args Information about the close operation. The FileHandle field must be set.
/// @param result Information returned from the operation specifying whether the operation completed successfully.
internal_function void
OsIoExecuteClose
(
    OS_IO_OPERATION          &args, 
    OS_IO_OPERATION_RESULT &result
)
{
    if (CloseHandle(args.FileHandle))
    {
        result.FileHandle = args.FileHandle;
        result.ResultCode = GetLastError();
        result.TransferAmount = 0;
        result.CompletedSynchronously = true;
        result.WasSuccessful = true;
    }
    else
    {
        result.FileHandle = args.FileHandle;
        result.ResultCode = GetLastError();
        result.TransferAmount = 0;
        result.CompletedSynchronously = true;
        result.WasSuccessful = false;
    }
}

/// @summary Prepare an OS_IO_RESULT to pass to the I/O completion callback after executing an I/O operation.
/// @param result The OS_IO_RESULT structure to populate.
/// @param request The OS_IO_REQUEST describing the I/O operation.
/// @param opres The OS_IO_OPERATION_RESULT describing the completion of the I/O operation.
internal_function void
OsIoPrepareResult
(
    OS_IO_RESULT          &result,
    OS_IO_REQUEST        *request, 
    OS_IO_OPERATION_RESULT &opres
)
{   // NOTE: this routine intentionally prepares the result to pass through as much
    // data as possible, even if that data might not have been needed for the request.
    result.RequestType = request->RequestType;
    result.ResultCode  = opres.ResultCode;
    result.UserContext = request->UserContext;
    result.FileHandle  = opres.FileHandle;
    result.PathBuffer  = request->PathBuffer;
    result.DataBuffer  = request->DataBuffer;
    result.DataAmount  = opres.TransferAmount;
    result.BaseOffset  = request->BaseOffset;
    result.FileOffset  = request->FileOffset;
}

/// @summary Execute an I/O request. The request may complete synchronously or asynchronously.
/// @param request The I/O request to execute.
/// @param completion_port The I/O completion port to associate with the request.
/// @param opres The structure to populate with the result of the I/O operation.
/// @param result The structure to populate with the result of the I/O request.
internal_function void
OsIoExecuteRequest
(
    OS_IO_REQUEST         *request, 
    HANDLE         completion_port,
    OS_IO_OPERATION_RESULT  &opres, 
    OS_IO_RESULT           &result
)
{
    switch (request->RequestType)
    {
        case OS_IO_REQUEST_NOOP:
            { // there's no actual I/O operation to perform.
              opres.FileHandle             = request->FileHandle;
              opres.ResultCode             = ERROR_SUCCESS;
              opres.TransferAmount         = 0;
              opres.CompletedSynchronously = true;
              opres.WasSuccessful          = true;
              OsIoPrepareResult(result, request, opres);
            } break;
        case OS_IO_REQUEST_OPEN_FILE:
            { // populate the operation request for the file open operation.
              // open operations return more data than most requests, and are always performed synchronously.
              FILE_STANDARD_INFO  fsi = {};
              OS_IO_OPERATION   opreq = {};
              size_t      sector_size = 0;
              opreq.FilePath          = request->PathBuffer;
              opreq.FileHandle        = INVALID_HANDLE_VALUE;
              opreq.CompletionPort    = completion_port;
              opreq.Overlapped        = NULL;
              opreq.DataBuffer        = NULL;
              opreq.FileOffset        = 0;
              opreq.PreallocationSize = request->DataAmount;
              opreq.TransferAmount    = 0;
              opreq.IoHintFlags       = request->IoHintFlags;
              OsIoExecuteOpen(opreq, opres, &fsi, &sector_size);
              OsIoPrepareResult(result, request, opres);
              result.FileSize = fsi.EndOfFile.QuadPart;
              result.PhysicalSectorSize = sector_size;
            } break;
        case OS_IO_REQUEST_READ_FILE:
            { // read requests may complete asynchronously.
              OS_IO_OPERATION   opreq = {};
              opreq.FilePath          = NULL;
              opreq.FileHandle        = request->FileHandle;
              opreq.CompletionPort    = NULL;
              opreq.Overlapped        =&request->Overlapped;
              opreq.DataBuffer        = request->DataBuffer;
              opreq.FileOffset        = request->BaseOffset + request->FileOffset;
              opreq.PreallocationSize = 0;
              opreq.TransferAmount    =(uint32_t) request->DataAmount;
              opreq.IoHintFlags       = OS_IO_HINT_FLAGS_NONE;
              OsIoExecuteRead(opreq, opres);
              OsIoPrepareResult(result, request, opres);
            } break;
        case OS_IO_REQUEST_WRITE_FILE:
            { // write requests may complete asynchronously.
              OS_IO_OPERATION   opreq = {};
              opreq.FilePath          = NULL;
              opreq.FileHandle        = request->FileHandle;
              opreq.CompletionPort    = NULL;
              opreq.Overlapped        =&request->Overlapped;
              opreq.DataBuffer        = request->DataBuffer;
              opreq.FileOffset        = request->BaseOffset + request->FileOffset;
              opreq.PreallocationSize = 0;
              opreq.TransferAmount    =(uint32_t) request->DataAmount;
              opreq.IoHintFlags       = OS_IO_HINT_FLAGS_NONE;
              OsIoExecuteWrite(opreq, opres);
              OsIoPrepareResult(result, request, opres);
            } break;
        case OS_IO_REQUEST_FLUSH_FILE:
            { // populate the operation request for the flush operation.
              // flush operations are always performed synchronously.
              OS_IO_OPERATION   opreq = {};
              opreq.FilePath          = NULL;
              opreq.FileHandle        = request->FileHandle;
              opreq.CompletionPort    = NULL;
              opreq.Overlapped        = NULL;
              opreq.DataBuffer        = NULL;
              opreq.FileOffset        = 0;
              opreq.PreallocationSize = 0;
              opreq.TransferAmount    = 0;
              opreq.IoHintFlags       = OS_IO_HINT_FLAGS_NONE;
              OsIoExecuteFlush(opreq, opres);
              OsIoPrepareResult(result, request, opres);
            } break;
        case OS_IO_REQUEST_CLOSE_FILE:
            { // populate the operation request for the close operation.
              // close operations are always performed synchronously.
              OS_IO_OPERATION   opreq = {};
              opreq.FilePath          = NULL;
              opreq.FileHandle        = request->FileHandle;
              opreq.CompletionPort    = NULL;
              opreq.Overlapped        = NULL;
              opreq.DataBuffer        = NULL;
              opreq.FileOffset        = 0;
              opreq.PreallocationSize = 0;
              opreq.TransferAmount    = 0;
              opreq.IoHintFlags       = OS_IO_HINT_FLAGS_NONE;
              OsIoExecuteClose(opreq, opres);
              OsIoPrepareResult(result, request, opres);
            } break;
        default:
            { // don't know what this is - just pass through data.
              opres.FileHandle             = request->FileHandle;
              opres.ResultCode             = ERROR_SUCCESS;
              opres.TransferAmount         = 0;
              opres.CompletedSynchronously = true;
              opres.WasSuccessful          = true;
              OsIoPrepareResult(result, request, opres);
            } break;
    }
}

/// @summary Return an I/O request to the pool it was allocated from.
/// @param request The I/O request to return.
internal_function void
OsReturnIoRequest
(
    OS_IO_REQUEST *request
)
{
    EnterCriticalSection(&request->RequestPool->ListLock);
    {   // remove the node from the live list.
        if (request->NextRequest != NULL)
            request->NextRequest->PrevRequest = request->PrevRequest;
        if (request->PrevRequest != NULL)
            request->PrevRequest->NextRequest = request->NextRequest;
        // push the node into the front of the free list.
        request->NextRequest = request->RequestPool->FreeRequest;
        request->PrevRequest = NULL;
        request->RequestPool->FreeRequest = request;
    }
    LeaveCriticalSection(&request->RequestPool->ListLock);
}

/// @summary Retrieve the OS_IO_REQUEST for an OVERLAPPED address passed back from GetQueuedCompletionStatus.
/// @param overlapped The OVERLAPPED instance corresponding to a completed request.
/// @return The associated OS_IO_REQUEST.
internal_function inline OS_IO_REQUEST*
OsIoRequestForOVERLAPPED
(
    OVERLAPPED *overlapped
)
{
    return ((OS_IO_REQUEST*)(((uint8_t*) overlapped) - offsetof(OS_IO_REQUEST, Overlapped)));
}

/// @summary Implement the entry point of an I/O worker thread.
/// @param argp An OS_IO_THREAD_INIT instance specifying data that should be copied to thread-local memory.
/// @return The function always returns 1.
internal_function unsigned int __cdecl
OsIoThreadMain
(
    void *argp
)
{
    OS_IO_THREAD_INIT   init = {};
    OS_IO_THREAD_POOL  *pool = NULL;
    OVERLAPPED           *ov = NULL;
    uintptr_t thread_context = 0;
    uintptr_t        context = 0;
    uintptr_t            key = 0;
    HANDLE       term_signal = NULL;
    HANDLE              iocp = NULL;
    LARGE_INTEGER  frequency = {};
    DWORD                tid = GetCurrentThreadId();
    DWORD             nbytes = 0;
    DWORD             termrc = 0;
    unsigned int   exit_code = 1;

    // copy the initialization data into local stack memory.
    // argp may have been allocated on the stack of the caller 
    // and is only guaranteed to remain valid until ReadySignal is set.
    CopyMemory(&init, argp, sizeof(OS_IO_THREAD_INIT));
    term_signal = init.TerminateSignal;
    context     = init.PoolContext;
    iocp        = init.CompletionPort;
    pool        = init.ThreadPool;

    // retrieve the system clock frequency for converting ticks to nanoseconds.
    QueryPerformanceFrequency(&frequency);

    // spit out a message just prior to initialization:
    OsLayerOutput("START: %S(%u): I/O thread starting on pool 0x%p.\n", __FUNCTION__, tid, pool);

    // perform application-level per-thread initialization.
    if (init.ThreadInit(pool, context, tid, &thread_context) < 0)
    {
        OsLayerError("ERROR: %S(%u): I/O thread initialization failed on pool 0x%p.\n", __FUNCTION__, tid, pool);
        OsLayerError("DEATH: %S(%u): I/O thread terminating in pool 0x%p.\n", __FUNCTION__, tid, pool);
        SetEvent(init.ErrorSignal);
        return 0;
    }

    // signal the main thread that this thread is ready to run.
    SetEvent(init.ReadySignal);

    __try
    {   // enter the main I/O loop. the thread waits for an event to be available on the completion port.
        for ( ; ; )
        {   // prior to possibly entering a wait state, check the termination signal.
            if ((termrc = WaitForSingleObject(term_signal, 0)) != WAIT_TIMEOUT)
            {   // either termination was signaled, or an error occurred.
                // either way, exit prior to entering the wait state.
                if (termrc != WAIT_OBJECT_0)
                {   // an error occurred while checking the termination signal.
                    OsLayerError("ERROR: %S(%u): Checking termination signal failed with result 0x%08X (0x%08X).\n", __FUNCTION__, tid, termrc, GetLastError());
                }
                break;
            }
            // wait for an event on the completion port.
            if (GetQueuedCompletionStatus(iocp, &nbytes, &key, &ov, INFINITE))
            {   // check the termination signal prior to processing the request.
                if ((termrc = WaitForSingleObject(term_signal, 0)) != WAIT_TIMEOUT)
                {   // either termination was signaled, or an error occurred.
                    if (termrc != WAIT_OBJECT_0)
                    {   // an error occurred while checking the termination signal.
                        OsLayerError("ERROR: %S(%u): Checking termination signal failed with result 0x%08X (0x%08X).\n", __FUNCTION__, tid, termrc, GetLastError());
                    }
                    break;
                }
                if (key == OS_COMPLETION_KEY_SHUTDOWN)
                {
                    OsLayerOutput("EVENT: %S(%u): I/O worker received shutdown signal.\n", __FUNCTION__, tid);
                    break;
                }

                OS_IO_REQUEST *request = OsIoRequestForOVERLAPPED(ov);
                while (request != NULL)
                {
                    switch (request->RequestState)
                    {
                        case OS_IO_REQUEST_STATE_CHAINED:
                        case OS_IO_REQUEST_STATE_SUBMITTED:
                            { // launch the I/O operation. it may complete asynchronously.
                              OS_IO_RESULT result;
                              OS_IO_OPERATION_RESULT opres;
                              OS_IO_REQUEST *chained = NULL;
                              request->RequestState  = OS_IO_REQUEST_STATE_LAUNCHED;
                              request->IoLaunchTime  = OsTimestampInTicks();
                              OsIoExecuteRequest(request, iocp, opres, result);
                              if (opres.CompletedSynchronously)
                              {   // update the request state and execute the completion callback.
                                  // there should be no additional events reported for this request.
                                  request->RequestState = OS_IO_REQUEST_STATE_COMPLETED;
                                  request->IoFinishTime = OsTimestampInTicks();
                                  if (request->IoComplete != NULL)
                                  {   // the request may return a chained request from its completion callback.
                                      OS_IO_REQUEST_CONTEXT ctx;
                                      OS_IO_PROFILE     profile;
                                      ctx.RequestPool          = request->RequestPool;
                                      ctx.ThreadPool           = pool;
                                      ctx.PoolContext          = context;
                                      ctx.ThreadContext        = thread_context;
                                      profile.QueueDelay       = OsElapsedNanoseconds(request->IoSubmitTime, request->IoLaunchTime, frequency.QuadPart);
                                      profile.ExecutionTime    = OsElapsedNanoseconds(request->IoLaunchTime, request->IoFinishTime, frequency.QuadPart);
                                      profile.OsThreadId       = tid;
                                      chained = request->IoComplete(opres.WasSuccessful, &result, &ctx, &profile);
                                  }
                                  OsReturnIoRequest(request);
                                  request = chained;
                              }
                              else
                              {   // since the request hasn't completed, there is no chained request.
                                  // don't return the request until it completes.
                                  request = NULL;
                              }
                            } break;
                        case OS_IO_REQUEST_STATE_LAUNCHED:
                            { // the asynchronous read or write operation has completed.
                              OS_IO_RESULT    result;
                              OS_IO_REQUEST *chained = NULL;
                              DWORD      result_code = ERROR_SUCCESS;
                              bool    was_successful = true;
                              if (!GetOverlappedResult(request->FileHandle, ov, &nbytes, FALSE))
                              {   // the I/O request may have completed unsuccessfully.
                                  if ((result_code   = GetLastError()) != ERROR_HANDLE_EOF)
                                      was_successful = false;
                              }
                              request->RequestState  = OS_IO_REQUEST_STATE_COMPLETED;
                              request->IoFinishTime  = OsTimestampInTicks();
                              result.RequestType     = request->RequestType;
                              result.ResultCode      = result_code;
                              result.UserContext     = request->UserContext;
                              result.FileHandle      = request->FileHandle;
                              result.PathBuffer      = request->PathBuffer;
                              result.DataBuffer      = request->DataBuffer;
                              result.DataAmount      = nbytes;
                              result.BaseOffset      = request->BaseOffset;
                              result.FileOffset      = request->FileOffset;
                              if (request->IoComplete != NULL)
                              {   // the request may return a chained request from its completion callback.
                                  OS_IO_REQUEST_CONTEXT ctx;
                                  OS_IO_PROFILE     profile;
                                  ctx.RequestPool          = request->RequestPool;
                                  ctx.ThreadPool           = pool;
                                  ctx.PoolContext          = context;
                                  ctx.ThreadContext        = thread_context;
                                  profile.QueueDelay       = OsElapsedNanoseconds(request->IoSubmitTime, request->IoLaunchTime, frequency.QuadPart);
                                  profile.ExecutionTime    = OsElapsedNanoseconds(request->IoLaunchTime, request->IoFinishTime, frequency.QuadPart);
                                  profile.OsThreadId       = tid;
                                  chained = request->IoComplete(was_successful, &result, &ctx, &profile);
                              }
                              OsReturnIoRequest(request);
                              request = chained;
                            } break;
                        case OS_IO_REQUEST_STATE_COMPLETED:
                            { OsLayerError("ERROR: %S(%u): I/O thread received already completed request %p from pool %p.\n", __FUNCTION__, GetCurrentThreadId(), request, request->RequestPool);
                              OsReturnIoRequest(request);
                            } break;
                        default:
                            { OsLayerError("ERROR: %S(%u): I/O thread received request %p from pool %p with unknown state %d.\n", __FUNCTION__, GetCurrentThreadId(), request, request->RequestPool, request->RequestState);
                            } break;
                    }
                }
            }
        }
    }
    __finally
    {   // the I/O thread is terminating - clean up thread-local resources.
        // ...
        // spit out a message just prior to termination.
        OsLayerError("DEATH: %S(%u): I/O thread terminating in pool 0x%p.\n", __FUNCTION__, tid, init.ThreadPool);
        return exit_code;
    }
}

/// @summary Search a list of layers for a given layer name to determine if a layer is supported.
/// @param layer_name A zero-terminated ASCII string specifying the registered name of the layer to locate.
/// @param layer_list The list of layer properties to search.
/// @param layer_count The number of layer properties to search.
/// @param layer_index If non-NULL, and the named layer is supported, this location is updated with the zero-based index of the layer in the list.
/// @return The layer name string (of the associated VkLayerProperties), if supported; otherwise, NULL.
internal_function char const*
OsSupportsVulkanLayer
(
    char              const *layer_name,
    VkLayerProperties const *layer_list, 
    size_t                   layer_count, 
    size_t                  *layer_index=NULL
)
{
    char const *s = NULL;
    for (size_t i = 0; i < layer_count; ++i)
    {
        if ((s = OsStringSearch(layer_list[i].layerName, layer_name)) != NULL)
        {
            if (layer_index != NULL) *layer_index = i;
            return s;
        }
    }
    return NULL;
}

/// @summary Search a list of extensions for a given extension name to determine if an extension is supported.
/// @param extension_name A zero-terminated ASCII string specifying the registered name of the extension to locate.
/// @param extension_list The list of extension properties to search.
/// @param extension_count The number of extension properties to search.
/// @param extension_index If non-NULL, and the named extension is supported, this location is updated with the zero-based index of the extension in the list.
/// @return The extension name string (of the associated VkExtensionProperties), if supported; otherwise, NULL.
internal_function char const*
OsSupportsVulkanExtension
(
    char                  const *extension_name,
    VkExtensionProperties const *extension_list, 
    size_t                       extension_count, 
    size_t                      *extension_index=NULL
)
{
    char const *s = NULL;
    for (size_t i = 0; i < extension_count; ++i)
    {
        if ((s = OsStringSearch(extension_list[i].extensionName, extension_name)) != NULL)
        {
            if (extension_index != NULL) *extension_index = i;
            return s;
        }
    }
    return NULL;
}

/// @summary Search a list of extension names for a specific extension name string.
/// @param extension_name A zero-terminated ASCII string specifying the registered name of the extension to locate.
/// @param extension_list The list of zero-terminated ASCII extension name strings to search.
/// @param extension_count The number of zero-terminated ASCII strings in the extension_list.
/// @return true if the named extension was found in the specified extension list.
internal_function bool
OsSupportsNamedExtension
(
    char   const         *extension_name, 
    char   const * const *extension_list, 
    size_t const          extension_count
)
{
    for (size_t i = 0; i < extension_count; ++i)
    {
        if (OsStringSearch(extension_list[i], extension_name) != NULL)
            return true;
    }
    return false;
}

/// @summary Determine whether the application has enabled an instance-level extension.
/// @param extension_name A zero-terminated ASCII string specifying the registered name of the extension to locate.
/// @param create_info The application-provided VkInstanceCreateInfo being supplied to vkCreateInstance.
/// @return true if the application has enabled the specified extension.
internal_function inline bool
OsVulkanInstanceExtensionEnabled
(
    char                 const *extension_name, 
    VkInstanceCreateInfo const    *create_info
)
{
    return OsSupportsNamedExtension(extension_name, create_info->ppEnabledExtensionNames, create_info->enabledExtensionCount);
}

/// @summary Determine whether the application has enabled a device-level extension.
/// @param extension_name A zero-terminated ASCII string specifying the registered name of the extension to locate.
/// @param create_info The application-provided VkDeviceCreateInfo being supplied to vkCreateDevice.
/// @return true if the application has enabled the specified extension.
internal_function inline bool
OsVulkanDeviceExtensionEnabled
(
    char               const *extension_name, 
    VkDeviceCreateInfo const *create_info
)
{
    return OsSupportsNamedExtension(extension_name, create_info->ppEnabledExtensionNames, create_info->enabledExtensionCount);
}

/// @summary Resolve functions exported directly from a Vulkan ICD.
/// @param runtime The OS_VULKAN_RUNTIME_DISPATCH instance, with the LoaderHandle field set.
/// @return VK_SUCCESS if all exported functions were resolved successfully.
internal_function VkResult
OsResolveVulkanExportFunctions
(
    OS_VULKAN_RUNTIME_DISPATCH *runtime
)
{
    OS_LAYER_RESOLVE_VULKAN_ICD_FUNCTION(runtime, vkGetInstanceProcAddr);
    return VK_SUCCESS;
}

/// @summary Resolve global functions exported by a Vulkan ICD.
/// @param runtime The OS_VULKAN_RUNTIME_DISPATCH instance, with the LoaderHandle field set.
/// @return VK_SUCCESS if all global functions were resolved successfully.
internal_function VkResult
OsResolveVulkanGlobalFunctions
(
    OS_VULKAN_RUNTIME_DISPATCH *runtime
)
{
    OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(runtime, vkCreateInstance);
    OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(runtime, vkEnumerateInstanceLayerProperties);
    OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(runtime, vkEnumerateInstanceExtensionProperties);
    return VK_SUCCESS;
}

/// @summary Resolve instance-level functions exported by a Vulkan runtime.
/// @param runtime The OS_VULKAN_RUNTIME_DISPATCH object, with the vkGetInstanceProcAddr entry point set.
/// @param instance The OS_VULKAN_INSTANCE_DISPATCH object, with the InstanceHandle field set.
/// @param create_info The VkInstanceCreateInfo being used to initialize the instance. Used here to resolve instance-level function pointers for extensions.
/// @return VK_SUCCESS if all instance-level functions were resolved successfully.
internal_function VkResult
OsResolveVulkanInstanceFunctions
(
    OS_VULKAN_RUNTIME_DISPATCH      *runtime,
    OS_VULKAN_INSTANCE_DISPATCH    *instance,
    VkInstanceCreateInfo  const *create_info 
)
{
    OS_LAYER_COPY_VULKAN_FUNCTION(instance, runtime, vkGetInstanceProcAddr);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkCreateDevice);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkDestroyInstance);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkEnumeratePhysicalDevices);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkEnumerateDeviceExtensionProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetDeviceProcAddr);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceFeatures);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceFormatProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceImageFormatProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceMemoryProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceQueueFamilyProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceSparseImageFormatProperties);
    if (OsVulkanInstanceExtensionEnabled(VK_KHR_SURFACE_EXTENSION_NAME, create_info))
    {   // resolve entry points for VK_KHR_surface.
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkDestroySurfaceKHR);
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceSurfaceSupportKHR);
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceSurfaceFormatsKHR);
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkGetPhysicalDeviceSurfacePresentModesKHR);
    }
    if (OsVulkanInstanceExtensionEnabled(VK_KHR_WIN32_SURFACE_EXTENSION_NAME, create_info))
    {   // resolve entry points for VK_KHR_win32_surface.
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkCreateWin32SurfaceKHR);
    }
    if (OsVulkanInstanceExtensionEnabled(VK_EXT_DEBUG_REPORT_EXTENSION_NAME, create_info))
    {   // resolve entry points for VK_EXT_debug_report.
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkCreateDebugReportCallbackEXT);
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkDebugReportMessageEXT);
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkDestroyDebugReportCallbackEXT);
    }
    return VK_SUCCESS;
}

/// @summary Resolve device-level functions exported by the Vulkan ICD.
/// @param vkinstance The OS_VULKAN_INSTANCE_DISPATCH object, with the vkGetDeviceProcAddr entry point set.
/// @param vkdevice The OS_VULKAN_DEVICE_DISPATCH object, with the LogicalDevice field set.
/// @param create_info The VkDeviceCreateInfo being used to initialize the logical device. Used here to resolve device-level function pointers for extensions.
/// @return VkResult if all device-level functions were resolved successfully.
internal_function VkResult
OsResolveVulkanDeviceFunctions
(
    OS_VULKAN_INSTANCE_DISPATCH *instance,
    OS_VULKAN_DEVICE_DISPATCH     *device, 
    VkDeviceCreateInfo const *create_info
)
{
    OS_LAYER_COPY_VULKAN_FUNCTION(device, instance, vkGetDeviceProcAddr);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkAllocateCommandBuffers);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkAllocateMemory);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkBeginCommandBuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkBindBufferMemory);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkBindImageMemory);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdBeginQuery);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdBeginRenderPass);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdBindDescriptorSets);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdBindIndexBuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdBindPipeline);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdBindVertexBuffers);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdBlitImage);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdClearAttachments);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdClearColorImage);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdClearDepthStencilImage);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdCopyBuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdCopyBufferToImage);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdCopyImage);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdCopyImageToBuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdCopyQueryPoolResults);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdDispatch);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdDispatchIndirect);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdDraw);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdDrawIndexed);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdDrawIndexedIndirect);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdDrawIndirect);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdEndQuery);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdEndRenderPass);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdExecuteCommands);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdFillBuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdNextSubpass);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdPipelineBarrier);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdPushConstants);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdResetEvent);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdResetQueryPool);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdResolveImage);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdSetBlendConstants);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdSetDepthBias);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdSetDepthBounds);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdSetEvent);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdSetLineWidth);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdSetScissor);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdSetStencilCompareMask);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdSetStencilReference);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdSetStencilWriteMask);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdSetViewport);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdUpdateBuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdWaitEvents);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCmdWriteTimestamp);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateBuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateBufferView);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateCommandPool);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateComputePipelines);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateDescriptorPool);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateDescriptorSetLayout);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateEvent);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateFence);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateFramebuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateGraphicsPipelines);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateImage);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateImageView);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreatePipelineCache);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreatePipelineLayout);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateQueryPool);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateRenderPass);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateSampler);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateSemaphore);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateShaderModule);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyBuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyBufferView);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyCommandPool);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyDescriptorPool);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyDescriptorSetLayout);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyDevice);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyEvent);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyFence);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyFramebuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyImage);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyImageView);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyPipeline);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyPipelineCache);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyPipelineLayout);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyQueryPool);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyRenderPass);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroySampler);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroySemaphore);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroyShaderModule);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDeviceWaitIdle);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkEndCommandBuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkFlushMappedMemoryRanges);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkFreeCommandBuffers);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkFreeDescriptorSets);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkFreeMemory);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetBufferMemoryRequirements);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetDeviceMemoryCommitment);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetDeviceQueue);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetEventStatus);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetFenceStatus);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetImageMemoryRequirements);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetImageSparseMemoryRequirements);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetImageSubresourceLayout);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetPipelineCacheData);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetQueryPoolResults);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetRenderAreaGranularity);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkInvalidateMappedMemoryRanges);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkMapMemory);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkMergePipelineCaches);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkQueueBindSparse);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkQueueSubmit);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkQueueWaitIdle);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkResetCommandBuffer);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkResetCommandPool);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkResetDescriptorPool);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkResetEvent);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkResetFences);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkSetEvent);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkUnmapMemory);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkUpdateDescriptorSets);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkWaitForFences);
    if (OsVulkanDeviceExtensionEnabled(VK_KHR_SWAPCHAIN_EXTENSION_NAME, create_info))
    {   // resolve entry points for VK_KHR_swapchain.
        OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkAcquireNextImageKHR);
        OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateSharedSwapchainsKHR);
        OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkCreateSwapchainKHR);
        OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkDestroySwapchainKHR);
        OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkGetSwapchainImagesKHR);
        OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(device, instance, vkQueuePresentKHR);
    }
    return VK_SUCCESS;
}

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @param Zero-fill a memory block.
/// @param dst The address of the block to zero-fill.
/// @param len The number of bytes to set to zero.
public_function void
OsZeroMemory
(
    void  *dst, 
    size_t len
)
{
    ZeroMemory(dst, len);
}

/// @summary Zero-fill a memory block in a way that is guaranteed not to be optimized out by the compiler.
/// @param dst The address of the block to zero-fill.
/// @param len The number of bytes to set to zero.
public_function void
OsSecureZeroMemory
(
    void  *dst,
    size_t len
)
{
    (void) SecureZeroMemory(dst, len);
}

/// @summary Copy memory from one block to another, where it is known that the source and destination address ranges do not overlap.
/// @param dst The address of the destination block.
/// @param src The address of the source block.
/// @param len The number of bytes to copy.
public_function void
OsCopyMemory
(
    void       * __restrict dst,
    void const * __restrict src, 
    size_t                  len
)
{
    CopyMemory(dst, src, len);
}

/// @summary Copy memory from one block to another, where the source and destination address ranges may overlap.
/// @param dst The address of the destination block.
/// @param src The address of the source block.
/// @param len The number of bytes to copy.
public_function void
OsMoveMemory
(
    void       *dst, 
    void const *src, 
    size_t      len
)
{
    MoveMemory(dst, src, len);
}

/// @summary Fill a block of memory with a given value.
/// @param dst The address of the start of the block to fill.
/// @param len The number of bytes to write.
/// @param val The value to write to each byte in the destination block.
public_function void
OsFillMemory
(
    void   *dst, 
    size_t  len, 
    uint8_t val
)
{
    FillMemory(dst, len, val);
}

/// @summary Rounds a size up to the nearest even multiple of a given power-of-two.
/// @param size The size value to round up.
/// @param pow2 The power-of-two alignment.
/// @return The input size, rounded up to the nearest even multiple of pow2.
public_function size_t 
OsAlignUp
(
    size_t size, 
    size_t pow2
)
{
    return (size == 0) ? pow2 : ((size + (pow2-1)) & ~(pow2-1));
}

/// @summary Retrieve the alignment required for a given type.
/// @typeparam T The type for which the required alignment is being queried.
/// @return The required alignment for the specified type, in bytes. This value is always a power of two.
template <typename T>
public_function inline size_t
OsAlignmentOfType
(
    void
)
{
    return std::alignment_of<T>::value;
}

/// @summary For a given address, return the address aligned for the specified type. The type T must be aligned to a power-of-two.
/// @param addr The unaligned address.
/// @return The address aligned to access elements of type T, or NULL if addr is NULL.
template <typename T>
public_function void* 
OsAlignFor
(
    void *addr
)
{
    const size_t a = std::alignment_of<T>::value;
    const size_t m = std::alignment_of<T>::value - 1;
    uint8_t     *p =(uint8_t*)addr;
    return  (addr != NULL) ? (void*) ((uintptr_t(p) + m) & ~m) : NULL;
}

/// @summary Calculate the worst-case requirement for allocating an instance of a structure, including padding.
/// @typeparam T The type being allocated. This type is used to determine the required alignment.
/// @return The number of bytes required to allocate an instance, including worst-case padding.
template <typename T>
public_function size_t
OsAllocationSizeForStruct
(
    void
)
{
    return sizeof(T) + (std::alignment_of<T>::value - 1);
}

/// @summary Calculate the worst-case requirement for allocating an array, including padding.
/// @typeparam T The type being allocated. This type is used to determine the required alignment.
/// @param n The number of items of type T in the array.
/// @return The number of bytes required to allocate an array of count items, including worst-case padding.
template <typename T>
public_function size_t
OsAllocationSizeForArray
(
    size_t n
)
{
    return (sizeof(T) * n) + (std::alignment_of<T>::value - 1);
}

/// @summary Initialize an OS_MEMORY_RANGE object for a committed block of memory.
/// @param addr The base address of the host-visible allocation.
/// @param size The size of the host-visible range, in bytes.
/// @return An OS_MEMORY_RANGE initialized with the specified memory block.
public_function inline OS_MEMORY_RANGE
OsInitHostMemoryRange
(
    void  *addr, 
    size_t size
)
{   assert(addr != NULL && size > 0);
    OS_MEMORY_RANGE r;
    r.HostAddress =(uint8_t*) addr;
    r.SizeInBytes = size;
    return r;
}

/// @summary Initialized an OS_MEMORY_RANGE to wrap an entire host memory allocation.
/// @param memory The host memory allocation to wrap.
/// @return An OS_MEMORY_RANGE initialized with the specified memory block.
public_function inline OS_MEMORY_RANGE
OsInitHostMemoryRange
(
    OS_HOST_MEMORY_ALLOCTION *memory
)
{   assert(memory->BaseAddress != NULL && memory->BytesCommitted > 0);
    OS_MEMORY_RANGE r;
    r.HostAddress = memory->BaseAddress;
    r.SizeInBytes = memory->BytesCommitted;
    return r;
}

/// @summary Initialize a pool of memory allocations.
/// @param pool The OS_HOST_MEMORY_POOL to initialize.
/// @param init The attributes of the pool.
/// @return Zero if the pool is successfully initialized, or -1 if an error occurred.
public_function int
OsCreateHostMemoryPool
(
    OS_HOST_MEMORY_POOL      *pool, 
    OS_HOST_MEMORY_POOL_INIT *init
)
{
    SYSTEM_INFO    sysinfo = {};
    size_t      total_size = 0;
    size_t actual_capacity = 0;
    void            *array = NULL;

    // retrieve the OS page size and allocation granularity.
    GetNativeSystemInfo(&sysinfo);

    // figure out how many bytes to allocate.
    total_size = OsAlignUp(init->PoolCapacity * sizeof(OS_HOST_MEMORY_ALLOCATION), sysinfo.dwPageSize);
    actual_capacity = total_size / sizeof(OS_HOST_MEMORY_ALLOCATION);

    // VirtualAlloc storage for all of the OS_HOST_MEMORY_ALLOCATION objects.
    if ((array = VirtualAlloc(NULL, total_size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Failed to allocate %Iu bytes for pool %S of %Iu items (%08X).\n", __FUNCTION__, OsThreadId(), total_size, init->PoolName, actual_capacity, GetLastError());
        return -1;
    }

    // initialize the fields of the OS_HOST_MEMORY_POOL object.
    pool->Name              = init->PoolName;
    pool->FreeList          = NULL;
    pool->NodeList          =(OS_HOST_MEMORY_ALLOCATION*) array;
    pool->Capacity          = actual_capacity;
    pool->MinAllocationSize = init->MinAllocationSize;
    pool->MinCommitIncrease = init->MinCommitIncrease;
    pool->PageSize          = sysinfo.dwPageSize;
    pool->Granularity       = sysinfo.dwAllocationGranularity;

    // initialize the pool free list.
    for (size_t i = 0; i < actual_capacity; ++i)
    {
        size_t ix = actual_capacity - i - 1;
        pool->NodeList[ix].SourcePool     = pool;
        pool->NodeList[ix].NextAllocation = pool->FreeList;
        pool->FreeList = &pool->NodeList[ix];
    }
    return 0;
}

/// @summary Free all memory associated with an OS_HOST_MEMORY_POOL object. All allocations are invalidated.
/// @param pool The OS_HOST_MEMORY_POOL to delete.
public_function void
OsDeleteHostMemoryPool
(
    OS_HOST_MEMORY_POOL *pool
)
{   // release all of the individual memory allocations.
    for (size_t i = 0, n = pool->Capacity; i < n; ++i)
    {
        OsHostMemoryRelease(&pool->NodeList[i]);
    }
    // release the memory allocated for the pool itself.
    if (pool->NodeList != NULL)
    {
        VirtualFree(pool->NodeList, 0, MEM_RELEASE);
    }
    pool->FreeList = NULL;
    pool->NodeList = NULL;
    pool->Capacity = 0;
}

/// @summary Reserve, and optionally commit, address space within a process.
/// @param pool The OS_HOST_MEMORY_POOL from which the OS_HOST_MEMORY_ALLOCATION will be acquired.
/// @param reserve_size The number of bytes of process address space to reserve. This value is rounded up to the nearest even multiple of the operating system page size.
/// @param commit_size The number of bytes of process address space to commit. This value is rounded up to the nearest even multiple of the operating system page size.
/// @param alloc_flags One or more of OS_HOST_MEMORY_ALLOCATION_FLAGS, or 0 if no special behavior is desired in which case the memory is readable, writable and has a guard page.
/// @return Zero if the address space is successfully reserved, or -1 if an error occurred.
public_function OS_HOST_MEMORY_ALLOCATION*
OsHostMemoryPoolAllocate
(
    OS_HOST_MEMORY_POOL *pool, 
    size_t       reserve_size, 
    size_t        commit_size, 
    uint32_t      alloc_flags
)
{
    if (pool->FreeList != NULL)
    {   // attempt to initialize the object at the head of the free list.
        OS_HOST_MEMORY_ALLOCATION *alloc = pool->FreeList;
        // attempt to initialize the object with the requested attributes.
        if (OsHostMemoryReserveAndCommit(alloc, reserve_size, commit_size, alloc_flags) < 0)
        {   // allocation failed. the error was already output.
            return NULL;
        }
        // pop the object from the head of the free list.
        pool->FreeList = alloc->NextAllocation;
        alloc->NextAllocation = NULL;
        return alloc;
    }
    else
    {   // the pool capacity needs to be increased; there are no free OS_HOST_MEMORY_ALLOCATION objects.
        OsLayerError("ERROR: %S(%u): No free OS_HOST_MEMORY_ALLOCATION objects in pool %S.\n", __FUNCTION__, OsThreadId(), pool->Name);
        return NULL;
    }
}

/// @summary Release all address space reserved and/or committed for an OS_HOST_MEMORY_ALLOCATION and return it to the free pool.
/// @param pool The pool to which the OS_HOST_MEMORY_ALLOCATION will be returned. This must be the same pool the allocation was acquired from.
/// @param alloc The OS_HOST_MEMORY_ALLOCATION to return.
public_function void
OsHostMemoryPoolRelease
(
    OS_HOST_MEMORY_POOL        *pool, 
    OS_HOST_MEMORY_ALLOCATION *alloc
)
{
    if (alloc->SourcePool != pool)
    {
        OsLayerError("ERROR: %S(%u): Returning allocation to incorrect pool %S.\n", __FUNCTION__, OsThreadId(), pool->Name);
        assert(alloc->SourcePool == pool && "OS_HOST_MEMORY_ALLOCATION released to wrong pool");
    }
    if (alloc->BaseAddress != NULL)
    {   // release all of the address space and return the chunk to the free pool.
        OsHostMemoryRelease(alloc);
        alloc->NextAllocation = pool->FreeList;
        pool->FreeList = alloc;
    }
}

/// @summary Reset a host memory pool to empty. All allocations and reservations are invalidated.
/// @param pool The OS_HOST_MEMORY_POOL to reset.
public_function void
OsHostMemoryPoolReset
(
    OS_HOST_MEMORY_POOL *pool
)
{   // empty the pool free list.
    pool->FreeList = NULL;
    // release all memory allocations and return them to the free list.
    for (size_t i = 0, n = pool->Capacity; i < n; ++i)
    {
        size_t ix = n - i - 1;
        OsHostMemoryRelease(&pool->NodeList[ix]);
        pool->NodeList[ix].SourcePool     = pool;
        pool->NodeList[ix].NextAllocation = pool->FreeList;
        pool->FreeList = &pool->NodeList[ix];
    }
}

/// @summary Reserve, and optionally commit, address space within a process. Call OsHostMemoryRelease first if the allocation currently holds a memory reservation.
/// @param alloc The OS_HOST_MEMORY_ALLOCATION to initialize. The OS_HOST_MEMORY_ALLOCTION::SourcePool and OS_HOST_MEMORY_ALLOCATION::NextAllocation fields are expected to be set by the caller.
/// @param reserve_size The number of bytes of process address space to reserve. This value is rounded up to the nearest even multiple of the operating system page size.
/// @param commit_size The number of bytes of process address space to commit. This value is rounded up to the nearest even multiple of the operating system page size.
/// @param alloc_flags One or more of OS_HOST_MEMORY_ALLOCATION_FLAGS, or 0 if no special behavior is desired in which case the memory is readable, writable and has a guard page.
/// @return Zero if the address space is successfully reserved, or -1 if an error occurred.
public_function int
OsHostMemoryReserveAndCommit
(
    OS_HOST_MEMORY_ALLOCATION *alloc, 
    size_t              reserve_size, 
    size_t               commit_size, 
    uint32_t             alloc_flags
)
{   assert(alloc->SourcePool != NULL);
    void   *base = NULL;
    size_t  page = alloc->SourcePool->PageSize;
    size_t extra = 0;
    DWORD access = 0;
    DWORD  flags = MEM_RESERVE;

    if (commit_size > reserve_size)
    {
        OsLayerError("ERROR: %S(%u): Requested commit size %Iu exceeds reserve size %Iu.\n", __FUNCTION__, OsThreadId(), commit_size, reserve_size);
        return -1;
    }

    // VMM allocations are rounded up to the next even multiple of the system 
    // page size, and have a starting address that is an even multiple of the
    // allocation granularity (typically 64KB.)
    reserve_size = OsAlignUp(reserve_size, page);

    // determine the VirtualAlloc protection flags for the reserved range of address space.
    if (alloc_flags & OS_HOST_MEMORY_ALLOCATION_FLAG_READ)
    {   // assume read-only access. access is upgraded if additional flags are set.
        access = PAGE_READONLY;
    }
    if (alloc_flags & OS_HOST_MEMORY_ALLOCATION_FLAG_WRITE)
    {   // write access implies read access to the memory.
        access = PAGE_READWRITE;
    }
    if (alloc_flags & OS_HOST_MEMORY_ALLOCATION_FLAG_EXECUTE)
    {   // execute implies read and write access to the memory.
        // additionally, this forces the entire reservation to be committed.
        access = PAGE_EXECUTE_READWRITE;
        commit_size = reserve_size;
    }
    if (alloc_flags == OS_HOST_MEMORY_ALLOCATION_FLAGS_NONE)
    {   // use the default access; the memory is readable and writable.
        access = PAGE_READWRITE;
    }

    // determine whether a guard page will be allocated for this allocation.
    if (alloc_flags & OS_HOST_MEMORY_ALLOCATION_FLAG_NO_GUARD_PAGE)
    {   // don't include a guard page.
        extra = 0;
    }
    else
    {   // include an extra page at the end of the reserved address space range.
        extra = page;
    }

    if (commit_size > 0)
    {   // memory will be both reserved and committed.
        commit_size = OsAlignUp(commit_size, page);
        flags |= MEM_COMMIT;
    }

    // reserve (and possibly commit) contiguous virtual address space.
    if ((base = VirtualAlloc(NULL, reserve_size+extra, flags, access)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): VirtualAlloc for %Iu bytes failed (%08X).\n", __FUNCTION__, OsThreadId(), reserve_size+extra, GetLastError());
        return -1;
    }
    if (extra > 0)
    {   // change the protection flags for the guard page only.
        if (VirtualAlloc((uint8_t*) base+reserve_size, page, MEM_COMMIT, access | PAGE_GUARD) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Failed to create guard page (%08X).\n", __FUNCTION__, OsThreadId(), GetLastError());
            VirtualFree(base, 0, MEM_RELEASE);
            return -1;
        }
    }

    // initialize the OS_HOST_MEMORY_ALLOCATION fields.
    alloc->BaseAddress     =(uint8_t*) base;
    alloc->BytesReserved   = reserve_size;
    alloc->BytesCommitted  = commit_size;
    alloc->AllocationFlags = alloc_flags;
    return 0;
}

/// @summary Increase the amount of committed memory within an allocation. The commit size cannot exceed the reserve size.
/// @param alloc The OS_HOST_MEMORY_ALLOCATION having its commit size increased.
/// @param commit_size The total amount of memory within the allocation that should be committed.
/// @return Zero if at least the specified amount of address space is successfully committed, or -1 if an error occurred.
public_function int
OsHostMemoryIncreaseCommitment
(
    OS_HOST_MEMORY_ALLOCATION *alloc, 
    size_t               commit_size
)
{
    if (alloc->BytesCommitted < commit_size)
    {   // VMM calls are expensive, so enforce a minimum commit increase.
        size_t const MIN_COMMIT_INCREASE = alloc->SourcePool->MinCommitIncrease;
        size_t const max_commit_increase = alloc->BytesReserved - alloc->BytesCommitted;
        size_t       req_commit_increase = commit_size          - alloc->BytesCommitted;
        if (req_commit_increase < MIN_COMMIT_INCREASE)
        {   // request the minimum commit increase.
            req_commit_increase = MIN_COMMIT_INCREASE;
        }
        if (req_commit_increase > max_commit_increase)
        {   // limit to the maximum possible commit increase.
            req_commit_increase = max_commit_increase;
        }
        size_t new_bytes_commit = OsAlignUp(alloc->BytesCommitted + req_commit_increase, alloc->SourcePool->PageSize);
        DWORD            access = 0;
        if (alloc->AllocationFlags & OS_HOST_MEMORY_ALLOCATION_FLAG_READ)
        {   // assume read-only access. access is upgraded if additional flags are set.
            access = PAGE_READONLY;
        }
        if (alloc->AllocationFlags & OS_HOST_MEMORY_ALLOCATION_FLAG_WRITE)
        {   // write access implies read access to the memory.
            access = PAGE_READWRITE;
        }
        if (alloc->AllocationFlags & OS_HOST_MEMORY_ALLOCATION_FLAG_EXECUTE)
        {   // execute implies read and write access to the memory.
            access = PAGE_EXECUTE_READWRITE;
        }
        if (alloc->AllocationFlags == OS_HOST_MEMORY_ALLOCATION_FLAGS_NONE)
        {   // use the default access; the memory is readable and writable.
            access = PAGE_READWRITE;
        }
        // request that an additional portion of the pre-reserved address space be committed.
        // executable allocations are entirely committed up-front, so no need to worry about that case here.
        if (VirtualAlloc((uint8_t*) alloc->BaseAddress, new_bytes_commit, MEM_COMMIT, access) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Failed to increase commit size to %Iu from %Iu.\n", __FUNCTION__, OsThreadId(), new_bytes_commit, alloc->BytesCommitted);
            return -1;
        }
        // the commitment amount was increased successfully.
        alloc->BytesCommitted = new_bytes_commit;
        return 0;
    }
    else
    {   // the requested commitment has already been met.
        return 0;
    }
}

/// @summary Flush the CPU instruction cache after dynamically generated code has been written to a memory allocation with the EXECUTE flag set.
/// @param alloc The OS_HOST_MEMORY_ALLOCATION containing the dynamically-generated code.
public_function void
OsHostMemoryFlush
(
    OS_HOST_MEMORY_ALLOCATION *alloc
)
{
    if (alloc->AllocationFlags & OS_HOST_MEMORY_ALLOCATION_FLAG_EXECUTE)
    {
        if (!FlushInstructionCache(GetCurrentProcess(), alloc->BaseAddress, alloc->BytesCommitted))
        {
            OsLayerError("ERROR: %S(%u): Failed to flush instruction cache (%08X).\n", __FUNCTION__, OsThreadId(), GetLastError());
        }
    }
}

/// @summary Release the process address space associated with a host memory allocation.
/// @param alloc The OS_HOST_MEMORY_ALLOCATION to release.
public_function void
OsHostMemoryRelease
(
    OS_HOST_MEMORY_ALLOCATION *alloc
)
{
    if (alloc->BaseAddress != NULL)
    {   // free the entire reserved range of virtual address space.
        VirtualFree(alloc->BaseAddress, 0, MEM_RELEASE);
    }
    alloc->BaseAddress    = NULL;
    alloc->BytesReserved  = 0;
    alloc->BytesCommitted = 0;
}

/// @summary Initialize an OS_ARENA_ALLOCATOR.
/// @param alloc The OS_ARENA_ALLOCATOR to initialize.
/// @param size_in_bytes The number of bytes from which the arena will sub-allocate.
/// @return Zero if the allocator is successfully initialized, or -1 if an error occurred.
public_function int
OsCreateArenaAllocator
(
    OS_ARENA_ALLOCATOR *alloc, 
    size_t      size_in_bytes
)
{
    alloc->NextOffset  = 0;
    alloc->SizeInBytes = size_in_bytes;
    return 0;
}

/// @summary Free resources associated with an arena allocator.
/// @param alloc The OS_ARENA_ALLOCATOR to delete.
public_function void
OsDeleteArenaAllocator
(
    OS_ARENA_ALLOCATOR *alloc
)
{
    alloc->NextOffset  = 0;
    alloc->SizeInBytes = 0;
}

/// @summary Determine whether an arena allocator can satisfy an allocation request.
/// @param alloc The OS_ARENA_ALLOCATOR to query.
/// @param size The minimum number of bytes to reserve.
/// @param alignment The required alignment of the returned block offset.
/// @return true if the arena can satisfy the allocation request.
public_function bool
OsArenaAllocatorCanSatisfyAllocation
(
    OS_ARENA_ALLOCATOR *alloc, 
    size_t               size, 
    size_t          alignment
)
{
    size_t aligned_address = OsAlignUp(alloc->NextOffset, alignment);
    size_t     alloc_bytes = size + (aligned_address - alloc->NextOffset);
    size_t      new_offset = alloc_bytes + alloc->NextOffset;
    return (new_offset <= alloc->SizeInBytes);
}

/// @summary Reserve space from an arena allocator.
/// @param alloc The OS_ARENA_ALLOCATOR to update.
/// @param size The minimum number of bytes to reserve.
/// @param alignment The required alignment of the returned memory block offset.
/// @param range On return, the ByteOffset and SizeInBytes fields are set to the offset and size of the allocated region.
/// @return true if the arena satisfied the allocation request.
public_function bool
OsArenaAllocate
(
    OS_ARENA_ALLOCATOR *alloc, 
    size_t               size, 
    size_t          alignment, 
    OS_MEMORY_RANGE    &range
)
{
    size_t aligned_address = OsAlignUp(alloc->NextOffset, alignment);
    size_t     alloc_bytes = size + (aligned_address - alloc->NextOffset);
    size_t      new_offset = alloc_bytes + alloc->NextOffset;
    if (new_offset <= alloc->SizeInBytes)
    {   // the allocation was satisfied, return the new info.
        range.ByteOffset   = aligned_address;
        range.SizeInBytes  = size;
        alloc->NextOffset  = new_offset;
        return true;
    }
    else
    {   // not enough space to satisfy the allocation.
        range.ByteOffset   = 0;
        range.SizeInBytes  = 0;
        return false;
    }
}

/// @summary Retrieve a marker representing the state of the arena allocator at the current point in time.
/// @param alloc The OS_ARENA_ALLOCATOR to query.
/// @return A marker that can be passed to OsArenaResetToMarker to reset the allocator back to its current state.
public_function os_arena_marker_t 
OsArenaMark
(
    OS_ARENA_ALLOCATOR *alloc
)
{
    return alloc->NextOffset;
}

/// @summary Reset an arena allocator back to a past point in time, invalidating all allocations made from that point forward.
/// @param alloc The OS_ARENA_ALLOCATOR to reset.
/// @param marker A marker returned by a previous call to OsArenaMark.
public_function void
OsArenaResetToMarker
(
    OS_ARENA_ALLOCATOR *alloc, 
    os_arena_marker_t  marker
)
{   assert(marker <= alloc->NextOffset);
    alloc->NextOffset = marker;
}

/// @summary Reset an arena allocator to empty.
/// @param alloc The OS_ARENA_ALLOCATOR to reset.
public_function void
OsArenaReset
(
    OS_ARENA_ALLOCATOR *alloc
)
{
    alloc->NextOffset = 0;
}

/// @summary Push a block offset onto the free list for a given level.
/// @param alloc The OS_BUDDY_ALLOCATOR instance to which the free block is being returned.
/// @param offset The byte offset of the start of the allocated block.
/// @param level The zero-based index of the level to which the block belongs.
internal_function inline void
OsBuddyAllocatorPushFreeOffset
(
    OS_BUDDY_ALLOCATOR *alloc, 
    uint32_t           offset, 
    uint32_t            level
)
{
    uint32_t *freelist = alloc->FreeLists[level];
    uint32_t    &count = alloc->FreeCount[level];
    freelist[count++]  = offset;
}

/// @summary Pop a block offset from the free list for a given level. The caller must ensure that the free list for the specified level is not empty.
/// @param alloc The OS_BUDDY_ALLOCATOR instance from which the free block is being taken.
/// @param level The zero-based index of the level to which the block belongs.
/// @return The byte offset of a free block.
internal_function inline uint32_t
OsBuddyAllocatorPopFreeOffset
(
    OS_BUDDY_ALLOCATOR *alloc, 
    uint32_t            level
)
{
    uint32_t *freelist = alloc->FreeLists[level];
    uint32_t    &count = alloc->FreeCount[level];
    return freelist[--count];
}

/// @summary Retrieve information about a memory block where the corresponding level index is known.
/// @param info The OS_BUDDY_BLOCK_INFO to populate.
/// @param alloc The OS_BUDDY_ALLOCATOR from which the block was allocated.
/// @param block_offset The offset of the allocation from the start of the memory range, as returned by a prior call to OsBuddyAllocate.
/// @param level The zero-based index of the level from which the block was allocated.
internal_function void
OsBuddyAllocatorBlockInfo
(
    OS_BUDDY_BLOCK_INFO *info, 
    OS_BUDDY_ALLOCATOR *alloc, 
    uint32_t     block_offset,
    uint32_t            level
)
{
    uint32_t     level_shift = alloc->LevelBits[level];
    uint32_t     block_count = 1 << level;
    uint32_t     local_index = block_offset >> level_shift;
    uint32_t       odd_index = local_index &  1;
    int32_t     buddy_offset = odd_index   ? -1 : +1;
    info->LevelIndex         = level;
    info->BitIndex           = level_shift;
    info->BlockSize          = 1 << level_shift;
    info->BlockCount         = block_count;
    info->IndexOffset        = block_count - 1;
    info->LeftAbsoluteIndex  = block_count + (local_index - odd_index) - 1;
    info->BlockAbsoluteIndex = block_count +  local_index - 1;
    info->BuddyAbsoluteIndex = block_count + (local_index + buddy_offset) - 1;
}

/// @summary Retrieve the information necessary to look up the status bit in the buddy allocator merge index for a given block.
/// @param info The OS_BUDDY_BLOCK_MERGE_INFO to populate.
/// @param block Information about the memory block, retrieved by a prior call to OsBuddyAllocatorBlockInfo.
internal_function inline void
OsBuddyAllocatorMergeIndexInfo
(
    OS_BUDDY_BLOCK_MERGE_INFO  *info, 
    OS_BUDDY_BLOCK_INFO const &block
)
{
    info->WordIndex =  block.LeftAbsoluteIndex >> 5;
    info->Mask = 1 << (block.LeftAbsoluteIndex & 31);
}

/// @summary Retrieve the information necessary to look up the status bit in the buddy allocator split index for a given block.
/// @param info The OS_BUDDY_BLOCK_SPLIT_INFO to populate.
/// @param block Information about the memory block, retrieved by a prior call to OsBuddyAllocatorBlockInfo.
internal_function inline void
OsBuddyAllocatorSplitIndexInfo
(
    OS_BUDDY_BLOCK_SPLIT_INFO  *info, 
    OS_BUDDY_BLOCK_INFO const &block
)
{
    info->WordIndex =  block.BlockAbsoluteIndex >> 5;
    info->Mask = 1 << (block.BlockAbsoluteIndex & 31);
}

/// @summary Initialize a buddy allocator instance.
/// @param alloc The OS_BUDDY_ALLOCATOR to initialize.
/// @param init Data specifying the allocator configuration.
/// @return Zero if the allocator is initialized successfully, or -1 if an error occurred.
public_function int
OsCreateBuddyAllocator
(
    OS_BUDDY_ALLOCATOR     *alloc, 
    OS_BUDDY_ALLOCATOR_INIT *init
)
{
    if ((init->AllocationSizeMin & (init->AllocationSizeMin-1)) != 0)
    {
        OsLayerError("ERROR: %S(%u): Buddy allocator requires Pow2 AllocationSizeMin.\n", __FUNCTION__, OsThreadId());
        assert((init->AllocationSizeMin & (init->AllocationSizeMin-1)) == 0);
        return -1;
    }
    if ((init->AllocationSizeMax & (init->AllocationSizeMax-1)) != 0)
    {
        OsLayerError("ERROR: %S(%u): Buddy allocator requires Pow2 AllocationSizeMax.\n", __FUNCTION__, OsThreadId());
        assert((init->AllocationSizeMax & (init->AllocationSizeMax-1)) == 0);
        return -1;
    }
    if (init->AllocationSizeMin < 16)
    {
        OsLayerError("ERROR: %S(%u): Buddy allocator requires AllocationSizeMin >= 16 bytes.\n", __FUNCTION__, OsThreadId());
        assert(init->AllocationSizeMin >= 16);
        return -1;
    }
    if (init->AllocationSizeMax <= init->AllocationSizeMin)
    {
        OsLayerError("ERROR: %S(%u): Maximum allocation size %Iu must be larger than minimum size %Iu.\n", __FUNCTION__, OsThreadId(), init->AllocationSizeMax, init->AllocationSizeMin);
        assert(init->AllocationSizeMax > init->AllocationSizeMin);
        return -1;
    }
    if (init->BytesReserved >= init->AllocationSizeMax)
    {
        OsLayerError("ERROR: %S(%u): Bytes reserved %Iu exceeds maximum allocation size %Iu.\n", __FUNCTION__, OsThreadId(), init->BytesReserved, init->AllocationSizeMax);
        assert(init->BytesReserved < init->AllocationSizeMax);
        return -1;
    }

    // figure out the number of levels and ensure the count doesn't exceed the limit (16).
    unsigned long max_bit = 0;
    unsigned long min_bit = 0;
#ifdef _M_X64
    _BitScanReverse64(&min_bit, init->AllocationSizeMin);
    _BitScanReverse64(&max_bit, init->AllocationSizeMax);
#else
    _BitScanReverse  (&min_bit, init->AllocationSizeMin);
    _BitScanReverse  (&max_bit, init->AllocationSizeMax);
#endif
    if ((max_bit - min_bit) > OS_BUDDY_ALLOCATOR::MAX_LEVELS)
    {   // need to adjust AllocationSizeMax/AllocationSizeMin.
        OsLayerError("ERROR: %S(%u): Level count %u exceeds maximum %Iu.\n", __FUNCTION__, OsThreadId(), (max_bit-min_bit), OS_BUDDY_ALLOCATOR::MAX_LEVELS);
        assert((max_bit-min_bit) <= OS_BUDDY_ALLOCATOR::MAX_LEVELS);
        return -1;
    }

    // determine the required size of the allocator metadata, and allocate it as one contiguous chunk.
    size_t level_bit   = max_bit;
    size_t level_count = max_bit - min_bit;
    size_t free_list_n =(1 <<  level_count) * 4;     // 4 = sizeof(uint32_t)
    size_t  ms_index_n =(1 << (level_count-1)) / 8;  // 8 = number of bits per-byte
    size_t  metadata_n = free_list_n + (2 * ms_index_n);
    void     *metadata = VirtualAlloc(NULL, metadata_n, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (metadata == NULL)
    {
        OsLayerError("ERROR: %S(%u): Failed to allocate %Iu bytes for buddy allocator metadata.\n", __FUNCTION__, OsThreadId(), metadata_n);
        return -1;
    }

    // initialize the simple allocator properties.
    alloc->AllocationSizeMin = init->AllocationSizeMin;
    alloc->AllocationSizeMax = init->AllocationSizeMax;
    alloc->BytesReserved     = init->BytesReserved;
    alloc->MetadataBase      =(uint8_t *) metadata;
    alloc->MergeIndex        =(uint32_t*)((uint8_t*) metadata + (ms_index_n * 0));
    alloc->SplitIndex        =(uint32_t*)((uint8_t*) metadata + (ms_index_n * 1));
    alloc->FreeListData      =(uint32_t*)((uint8_t*) metadata + (ms_index_n * 2));
    alloc->LevelCount        =(uint32_t) level_count;
    alloc->Reserved          = 0;

    // initialize the per-level allocator state data.
    for (size_t level_index  = 0; level_index < level_count; ++level_index)
    {
        alloc->FreeCount[level_index] = 0;
        alloc->LevelBits[level_index] =(uint32_t) level_bit--;
        alloc->FreeLists[level_index] = alloc->FreeListData + ((1 << level_index) - 1);
    }
    
    // push offset 0 onto the free list for level 0 (the largest level.)
    OsBuddyAllocatorPushFreeOffset(alloc, 0, 0);

    // sometimes the requirement of AllocationSizeMax being a power-of-two leads 
    // to signficant memory waste, so allow the caller to specify a BytesReserved
    // value to mark a portion of the memory as unusable.
    if (init->BytesReserved > 0)
    {   // allocate small blocks until BytesReserved is met.
        // allocating the smallest block size ensures the least amount of waste.
        // contiguous blocks will be allocated, starting from offset 0.
        OS_MEMORY_RANGE   r;
        uint32_t level_size  = 1 << (level_count - 1);
        uint32_t block_count =(uint32_t)((init->BytesReserved + level_size) / level_size);
        for (uint32_t block_index = 0; block_index < block_count;++block_index)
        {
            (void) OsBuddyAllocate(alloc, level_size, level_size, r);
        }
    }
    return 0;
}

/// @summary Free all resources associated with an OS_BUDDY_ALLOCATOR instance.
/// @param alloc The OS_BUDDY_ALLOCATOR to delete.
public_function void
OsDeleteBuddyAllocator
(
    OS_BUDDY_ALLOCATOR *alloc
)
{
    if (alloc->MetadataBase != NULL)
    {
        VirtualFree(alloc->MetadataBase, 0, MEM_RELEASE);
    }
    OsZeroMemory(alloc, sizeof(OS_BUDDY_ALLOCATOR));
}

/// @summary Retrieve information about a memory block where the corresponding level is not known.
/// @param info The OS_BUDDY_BLOCK_INFO to populate.
/// @param alloc The OS_BUDDY_ALLOCATOR from which the block was allocated.
/// @param block_offset The offset of the allocation from the start of the memory range, as returned by a prior call to OsBuddyAllocate.
/// @return true if the offset block_offset was valid and info was populated with data.
public_function bool
OsBuddyAllocatorBlockInfo
(
    OS_BUDDY_BLOCK_INFO *info, 
    OS_BUDDY_ALLOCATOR *alloc, 
    size_t       block_offset
)
{   assert(block_offset < alloc->AllocationSizeMax);
    OS_BUDDY_BLOCK_INFO       block;
    OS_BUDDY_BLOCK_SPLIT_INFO split;
    uint32_t  offset_u32 =(uint32_t)block_offset;
    uint32_t level_index = alloc->LevelCount - 1;
    while   (level_index > 0)
    {   // check the parent level to see if it has been split.
        OsBuddyAllocatorBlockInfo(&block, alloc, offset_u32, level_index - 1);
        OsBuddyAllocatorSplitIndexInfo(&split, block);
        if ((alloc->SplitIndex[split.WordIndex] & split.Mask) != 0)
        {   // reached a split parent; the block level is level_index.
            OsBuddyAllocatorBlockInfo(info, alloc, offset_u32, level_index);
            return true;
        }
        // the parent has not been split, so check the next-largest level.
        level_index--;
    }
    // this must be a level-0 allocation.
    OsBuddyAllocatorBlockInfo(&block, alloc, offset_u32, 0);
    return true;
}

/// @summary Allocate memory from a buddy allocator.
/// @param alloc The OS_BUDDY_ALLOCATOR managing the memory.
/// @param size The number of bytes being requested.
/// @param alignment The required alignment of the returned block offset.
/// @param range On return, the ByteOffset and SizeInBytes fields are set to the offset and size of the allocated region.
/// @return true if the allocator satisfied the request.
public_function bool
OsBuddyAllocate
(
    OS_BUDDY_ALLOCATOR *alloc, 
    size_t               size, 
    size_t          alignment, 
    OS_MEMORY_RANGE    &range
)
{
    if (size < alignment)
    {   // round upwards to the requested alignment.
        size = alignment;
    }
    if (size < alloc->AllocationSizeMin)
    {   // round up to the minimum possible block size.
        size = alloc->AllocationSizeMin;
    }
    if (size > alloc->AllocationSizeMax)
    {
        OsLayerError("ERROR: %S(%u): Allocation request for %Iu bytes exceeds maximum of %Iu bytes.\n", __FUNCTION__, OsThreadId(), size, alloc->AllocationSizeMax);
        assert(size <= alloc->AllocationSizeMax);
        goto no_free_block;
    }

    uint32_t pow2_size = (uint32_t) OsNextPowerOfTwoGreaterOrEqual(size);
    uint32_t bit_index;  _BitScanReverse((DWORD*) &bit_index, pow2_size);
    uint32_t level_idx = alloc->LevelBits[0] - bit_index;
    uint32_t check_idx = level_idx;
    for ( ; ; )
    {   // if the free list is not empty, the allocation can be satisfied.
        if (alloc->FreeCount[check_idx] > 0)
        {   // perform splits from check_idx (<= level_idx) to level_idx.
            // this process splits larger blocks into smaller blocks.
            while (check_idx < level_idx)
            {   // pop an offset from the free list at the parent block level.
                OS_BUDDY_BLOCK_INFO      parent_info;
                OS_BUDDY_BLOCK_SPLIT_INFO split_info;
                uint32_t  parent_offset = OsBuddyAllocatorPopFreeOffset(alloc, check_idx);
                OsBuddyAllocatorBlockInfo(&parent_info, alloc, parent_offset, check_idx);
                OsBuddyAllocatorSplitIndexInfo(&split_info, parent_info);

                // mark the larger parent block as having been split.
                alloc->SplitIndex[split_info.WordIndex] |= split_info.Mask;

                // insert two blocks into the free list of the next-smaller level.
                OsBuddyAllocatorPushFreeOffset(alloc, parent_offset                               , check_idx+1);
                OsBuddyAllocatorPushFreeOffset(alloc, parent_offset + (parent_info.BlockSize >> 1), check_idx+1);
                check_idx++;
            }

            // return a block from level_idx, which is now known to have at least one free block.
            OS_BUDDY_BLOCK_INFO       block_info;
            OS_BUDDY_BLOCK_MERGE_INFO merge_info;
            uint32_t  return_offset = OsBuddyAllocatorPopFreeOffset(alloc, level_idx);
            OsBuddyAllocatorBlockInfo(&block_info, alloc, return_offset, level_idx);
            OsBuddyAllocatorMergeIndexInfo(&merge_info, block_info);

            // toggle the buddy pair bit representing the pair allocation state.
            // if both blocks are allocated, the corresponding bit will be set to 0.
            // if only return_offset is allocated, the corresponding bit will be set to 1.
            alloc->MergeIndex[merge_info.WordIndex] ^= merge_info.Mask;
            range.ByteOffset  = return_offset;
            range.SizeInBytes = pow2_size;
            return true;
        }
        if (check_idx != 0)
        {   // check the next larger level to see if there are any free blocks.
            check_idx--;
        }
        else break;
    }

no_free_block:
    // there is no free block that can satisfy the allocation.
    range.ByteOffset  = 0;
    range.SizeInBytes = 0;
    return false;
}

/// @summary Grow or shrink a memory block to meet a desired size.
/// @param alloc The OS_BUDDY_ALLOCATOR managing the allocated range.
/// @param existing An OS_MEMORY_RANGE describing the existing allocation.
/// @param new_size The new required minimum allocation size, in bytes.
/// @param alignment The required alignment of the returned block offset.
/// @param range On return, the ByteOffset and SizeInBytes fields are set to the offset and size of the allocated region, which may or may not be the same as the existing region.
/// @return true if the allocator satisfied the request.
public_function bool
OsBuddyReallocate
(
    OS_BUDDY_ALLOCATOR *alloc, 
    OS_MEMORY_RANGE  existing, 
    size_t           new_size, 
    size_t          alignment,
    OS_MEMORY_RANGE    &range
)
{
    if (existing.SizeInBytes == 0)
    {   // there is no existing allocation, so this is equivalent to calling OsBuddyAllocate.
        return OsBuddyAllocate(alloc, new_size, alignment, range);
    }
    if (new_size < alignment)
    {   // round upwards to the requested alignment.
        new_size = alignment;
    }
    if (new_size < alloc->AllocationSizeMin)
    {   // round up to the minimum possible block size.
        new_size = alloc->AllocationSizeMin;
    }
    if (new_size > alloc->AllocationSizeMax)
    {
        OsLayerError("ERROR: %S(%u): Reallocation request for %Iu bytes exceeds maximum of %Iu bytes.\n", __FUNCTION__, OsThreadId(), new_size, alloc->AllocationSizeMax);
        assert(new_size <= alloc->AllocationSizeMax);
        goto no_free_block;
    }

    // there are four scenarios this routine has to account for:
    // 1. The new_size still fits in the same block. No re-allocation is performed.
    // 2. The new_size is larger than the old size, but fits in a block one level larger, and the buddy block is free. The buddy block is allocated, and the buddy pair is merged. No copying is required.
    // 3. The new_size is smaller than the old size by one or more levels. The existing block is demoted to a smaller block. No copying is required.
    // 4. The new_size is larger than the old size by more than one level, or the buddy was not free. A new, larger block is allocated and the existing block is freed. The caller must copy the data from the old to the new block.
    uint32_t pow2_size_old = (uint32_t) OsNextPowerOfTwoGreaterOrEqual(existing.SizeInBytes);
    uint32_t pow2_size_new = (uint32_t) OsNextPowerOfTwoGreaterOrEqual(new_size);
    uint32_t bit_index_old;  _BitScanReverse((DWORD*) &bit_index_old, pow2_size_old);
    uint32_t bit_index_new;  _BitScanReverse((DWORD*) &bit_index_new, pow2_size_new);
    uint32_t level_idx_old = alloc->LevelBits[0] - bit_index_old;
    uint32_t level_idx_new = alloc->LevelBits[0] - bit_index_new;
    
    if (level_idx_new == level_idx_old)
    {   // case 1: the new_size fits in the same block. don't do anything.
        range.ByteOffset  = existing.ByteOffset;
        range.SizeInBytes = pow2_size_new;
        return true;
    }
    if (level_idx_new ==(level_idx_old-1))
    {   // case 2: see if the buddy is free, and if so, promote the existing block.
        OS_BUDDY_BLOCK_INFO        block_info;
        OS_BUDDY_BLOCK_MERGE_INFO  merge_info;
        OS_BUDDY_BLOCK_SPLIT_INFO  split_info;
        OsBuddyAllocatorBlockInfo(&block_info, alloc, (uint32_t) existing.ByteOffset, level_idx_old);
        OsBuddyAllocatorMergeIndexInfo(&merge_info, block_info);
        if((alloc->MergeIndex[merge_info.WordIndex] & merge_info.Mask) != 0)
        {   // the buddy block is free - merge it with the existing block.
            // toggle the status bit to 0 - both blocks are allocated.
            alloc->MergeIndex[merge_info.WordIndex] ^= merge_info.Mask;

            // scan the free list to locate the buddy block offset, and remove it.
            uint32_t merge_offset = (block_info.LeftAbsoluteIndex  - block_info.IndexOffset) * block_info.BlockSize;
            uint32_t buddy_offset = (block_info.BuddyAbsoluteIndex - block_info.IndexOffset) * block_info.BlockSize;
            uint32_t *free_offset =  alloc->FreeLists[level_idx_old];
            for (size_t i = 0,  n =  alloc->FreeCount[level_idx_old]; i < n; ++i)
            {
                if (free_offset[i] == buddy_offset)
                {   // found the matching offset. swap the last item into its place.
                    free_offset[i] = free_offset[n-1];
                    alloc->FreeCount[level_idx_old]--;
                    break;
                }
            }

            // retrieve the attributes of the parent block.
            OsBuddyAllocatorBlockInfo(&block_info, alloc, merge_offset, level_idx_new);
            OsBuddyAllocatorMergeIndexInfo(&merge_info, block_info);
            OsBuddyAllocatorSplitIndexInfo(&split_info, block_info);

            // mark the parent block as being allocated, and and clear its split status.
            alloc->MergeIndex[merge_info.WordIndex] ^= merge_info.Mask;
            alloc->SplitIndex[split_info.WordIndex] &=~split_info.Mask;

            // return the merged block.
            range.ByteOffset  = merge_offset;
            range.SizeInBytes = block_info.BlockSize;
            return true;
        }
    }
    if (level_idx_new > level_idx_old)
    {   // case 3: demote the existing block to a smaller size.
        OS_BUDDY_BLOCK_INFO        block_info;
        OS_BUDDY_BLOCK_MERGE_INFO  merge_info;
        OS_BUDDY_BLOCK_SPLIT_INFO  split_info;
        uint32_t                 existing_u32 = (uint32_t) existing.ByteOffset;
        OsBuddyAllocatorBlockInfo(&block_info, alloc, existing_u32, level_idx_old);
        OsBuddyAllocatorMergeIndexInfo(&merge_info, block_info);

        // mark the current block as being free.
        alloc->MergeIndex[merge_info.WordIndex] ^= merge_info.Mask;

        // perform splits down to the necessary block size.
        while (level_idx_old < level_idx_new)
        {   // update the split index to mark the parent block as having been split.
            OsBuddyAllocatorSplitIndexInfo(&split_info, block_info);
            alloc->SplitIndex[split_info.WordIndex] |= split_info.Mask;
            OsBuddyAllocatorBlockInfo(&block_info, alloc, existing_u32, block_info.LevelIndex + 1);
            OsBuddyAllocatorPushFreeOffset(alloc, existing_u32 + block_info.BlockSize, block_info.LevelIndex);
            level_idx_old++;
        }

        // mark the new smaller block as allocated in the merge index.
        OsBuddyAllocatorMergeIndexInfo(&merge_info, block_info);
        alloc->MergeIndex[merge_info.WordIndex] ^= merge_info.Mask;
        range.ByteOffset  = existing.ByteOffset;
        range.SizeInBytes = block_info.BlockSize;
        return true;
    }

    // case 4: no choice but to allocate a new block and free the old block.
    if (OsBuddyAllocate(alloc, new_size, alignment, range))
    {   // the new block was successfully allocated, so mark the old block as free.
        OsBuddyFree(alloc, existing);
        return true;
    }
    // else, fall through to no_free block.
no_free_block:
    range.ByteOffset  = 0;
    range.SizeInBytes = 0;
    return false;
}

/// @summary Retrieve the size for a given byte offset returned by the allocator.
/// @param alloc The OS_BUDDY_ALLOCATOR from which the memory was allocated.
/// @param block_offset A memory block offset returned by a prior call to OsBuddyAllocate or OsBuddyReallocate.
/// @return The size of the specified block, in bytes, or 0 if the specified block offset is invalid.
public_function size_t
OsBuddyBlockSize
(
    OS_BUDDY_ALLOCATOR *alloc, 
    size_t       block_offset
)
{
    OS_BUDDY_BLOCK_INFO info;
    if (OsBuddyAllocatorBlockInfo(&info, alloc, block_offset))
        return info.BlockSize;
    else
        return 0;
}

/// @summary Free a previously allocated memory range.
/// @param alloc The OS_BUDDY_ALLOCATOR that returned the memory range.
/// @param range The block offset and size returned by a prior call to OsBuddyAllocate or OsBuddyReallocate.
public_function void
OsBuddyFree
(
    OS_BUDDY_ALLOCATOR *alloc, 
    OS_MEMORY_RANGE     range
)
{
    if (range.SizeInBytes > 0)
    {   // ensure that the specified size is at least the minimum allocation size.
        if (range.SizeInBytes < alloc->AllocationSizeMin)
            range.SizeInBytes = alloc->AllocationSizeMin;

        OS_BUDDY_BLOCK_INFO       block_info;
        OS_BUDDY_BLOCK_MERGE_INFO merge_info;
        OS_BUDDY_BLOCK_SPLIT_INFO split_info;

        // convert the supplied size to a level index.
        uint32_t offset_u32= (uint32_t) range.ByteOffset;
        uint32_t pow2_size = (uint32_t) OsNextPowerOfTwoGreaterOrEqual(range.SizeInBytes);
        uint32_t bit_index;  _BitScanReverse((DWORD*) &bit_index, pow2_size);
        uint32_t level_idx = alloc->LevelBits[0] - bit_index;

        // mark the block as free in the merge index.
        OsBuddyAllocatorBlockInfo(&block_info, alloc, offset_u32, level_idx);
        OsBuddyAllocatorMergeIndexInfo(&merge_info, block_info);
        alloc->MergeIndex[merge_info.WordIndex] ^= merge_info.Mask;
        do
        {   // if the new state is 0, the block and its buddy can be merged.
            // if the new state is 1, only the block is free, and we're done.
            if ((block_info.LevelIndex == 0) || ((alloc->MergeIndex[merge_info.WordIndex] & merge_info.Mask) != 0))
            {   // no additional merging can be performed.
                break;
            }

            // a merge operation between a buddy pair can be performed.
            // remove the buddy block from the free list.
            // the merged block is the lowest offset of the pair.
            uint32_t merge_offset = (block_info.LeftAbsoluteIndex  - block_info.IndexOffset) * block_info.BlockSize;
            uint32_t buddy_offset = (block_info.BuddyAbsoluteIndex - block_info.IndexOffset) * block_info.BlockSize;
            uint32_t *free_offset =  alloc->FreeLists[block_info.LevelIndex];
            for (size_t i = 0,  n =  alloc->FreeCount[block_info.LevelIndex]; i < n; ++i)
            {
                if (free_offset[i] == buddy_offset)
                {   // found the matching offset. swap the last item into its place.
                    free_offset[i] = free_offset[n-1];
                    alloc->FreeCount[block_info.LevelIndex]--;
                    break;
                }
            }

            // clear the split status for the parent block.
            OsBuddyAllocatorBlockInfo(&block_info, alloc, merge_offset, block_info.LevelIndex - 1);
            OsBuddyAllocatorMergeIndexInfo(&merge_info, block_info);
            OsBuddyAllocatorSplitIndexInfo(&split_info, block_info);
            alloc->SplitIndex[split_info.WordIndex] &= ~split_info.Mask;

            // continue trying to merge into larger blocks.
            offset_u32 = merge_offset;
        } while (block_info.LevelIndex != 0);
        // return the possibly merged block to the free list for the level it was allocated from.
        OsBuddyAllocatorPushFreeOffset(alloc, offset_u32, block_info.LevelIndex);
    }
}

/// @summary Reset a buddy allocator back to its initial state, invalidating all existing allocations.
/// @param alloc The OS_BUDDY_ALLOCATOR to reset.
public_function void
OsBuddyReset
(
    OS_BUDDY_ALLOCATOR *alloc
)
{   // return the merge and split indexes to their initial state.
    OsZeroMemory(alloc->MergeIndex, (1 << (alloc->LevelCount-1)) / 8);
    OsZeroMemory(alloc->SplitIndex, (1 << (alloc->LevelCount-1)) / 8);

    // remove all items from the per-level free lists.
    OsZeroMemory(alloc->FreeCount, OS_BUDDY_ALLOCATOR::MAX_LEVELS * sizeof(uint32_t));
    
    // push offset 0 onto the free list for level 0 (the largest level.)
    OsBuddyAllocatorPushFreeOffset(alloc, 0, 0);

    // sometimes the requirement of AllocationSizeMax being a power-of-two leads 
    // to signficant memory waste, so allow the caller to specify a BytesReserved
    // value to mark a portion of the memory as unusable.
    if (alloc->BytesReserved > 0)
    {   // allocate small blocks until BytesReserved is met.
        // allocating the smallest block size ensures the least amount of waste.
        // contiguous blocks will be allocated, starting from offset 0.
        OS_MEMORY_RANGE   r;
        uint32_t level_size  = 1 << (alloc->LevelCount - 1);
        uint32_t block_count =(uint32_t)((alloc->BytesReserved + level_size) / level_size);
        for (uint32_t block_index = 0; block_index < block_count; ++block_index)
        {
            (void) OsBuddyAllocate(alloc, level_size, level_size, r);
        }
    }
}

/// @summary Reserve process address space for a memory arena. By default, no address space is committed.
/// @param arena The OS_HOST_MEMORY_ARENA to initialize.
/// @param host_memory The address and size of the host-visible memory block to sub-allocate from.
/// @return Zero if the arena is initialized, or -1 if an error occurred.
public_function inline int
OsCreateHostMemoryArena
(
    OS_HOST_MEMORY_ARENA *arena, 
    OS_MEMORY_RANGE host_memory
)
{   assert(host_memory.HostAddress != NULL);
    assert(host_memory.SizeInBytes >  0);
    arena->HostMemory = host_memory;
    return OsCreateArenaAllocator(&arena->Allocator, host_memory.SizeInBytes);
}

/// @summary Release process address space reserved for a memory arena. All allocations are invalidated.
/// @param arena The memory arena to delete.
public_function inline void
OsDeleteHostMemoryArena
(
    OS_HOST_MEMORY_ARENA *arena
)
{
    OsDeleteArenaAllocator(&arena->Allocator);
    OsZeroMemory(arena, sizeof(OS_HOST_MEMORY_ARENA));
}

/// @summary Determine whether a memory allocation request can be satisfied.
/// @param arena The memory arena to query.
/// @param size The size of the allocation request, in bytes.
/// @param alignment The desired alignment of the returned address. This must be a power of two greater than zero.
/// @return true if the specified allocation will succeed.
public_function inline bool
OsHostMemoryArenaCanSatisfyAllocation
(
    OS_HOST_MEMORY_ARENA *arena, 
    size_t                 size,
    size_t            alignment
)
{
    return OsArenaAllocatorCanSatisfyAllocation(&arena->Allocator, size, alignment);
}

/// @summary Determine whether a memory allocation request can be satisfied.
/// @typeparam T The type being allocated. This type is used to determine the required alignment.
/// @param arena The memory arena to query.
/// @return true if the specified allocation will succeed.
template <typename T>
public_function inline bool
OsHostMemoryArenaCanAllocate
(
    OS_HOST_MEMORY_ARENA *arena
)
{
    return OsArenaAllocatorCanSatisfyAllocation(&arena->Allocator, sizeof(T), std::alignment_of<T>::value);
}

/// @summary Determine whether a memory allocation request for an array can be satisfied.
/// @typeparam T The type of array element. This type is used to determine the required alignment.
/// @param arena The memory arena to query.
/// @param count The number of items in the array.
/// @return true if the specified allocation will succeed.
template <typename T>
public_function inline bool
OsHostMemoryArenaCanAllocateArray
(
    OS_HOST_MEMORY_ARENA *arena,
    size_t                count
)
{
    return OsArenaAllocatorCanSatisfyAllocation(&arena->Allocator, sizeof(T) * count, std::alignment_of<T>::value);
}

/// @summary Allocate memory from an arena. Additional address space is committed up to the initial reservation size.
/// @param arena The memory arena to allocate from.
/// @param size The minimum number of bytes to allocate.
/// @param alignment A power-of-two, greater than or equal to 1, specifying the alignment of the returned address.
/// @return A pointer to the start of the allocated block, or NULL if the request could not be satisfied.
public_function inline void*
OsHostMemoryArenaAllocate
(
    OS_HOST_MEMORY_ARENA *arena, 
    size_t                 size, 
    size_t            alignment
)
{
    OS_MEMORY_RANGE r;
    if (OsArenaAllocate(&arena->Allocator, size, alignment, r))
    {   // the allocation was successful. convert offset to address.
        return (uint8_t*) arena->HostMemory.HostAddress + r.ByteOffset;
    }
    return NULL;
}

/// @summary Allocate memory for a structure.
/// @typeparam T The type being allocated. This type is used to determine the required alignment.
/// @param arena The memory arena to allocate from.
/// @return A pointer to the new structure, or nullptr if the arena could not satisfy the allocation request.
template <typename T>
public_function inline T*
OsHostMemoryArenaAllocate
(
    OS_HOST_MEMORY_ARENA *arena
)
{
    return (T*) OsHostMemoryArenaAllocate(arena, sizeof(T), std::alignment_of<T>::value);
}

/// @summary Allocate memory for an array of structures.
/// @typeparam T The type of array element. This type is used to determine the required alignment.
/// @param arena The memory arena to allocate from.
/// @param count The number of items to allocate.
/// @return A pointer to the start of the array, or nullptr if the arena could not satisfy the allocation request.
template <typename T>
public_function inline T*
OsHostMemoryArenaAllocateArray
(
    OS_HOST_MEMORY_ARENA *arena, 
    size_t                count
)
{
    return (T*) OsHostMemoryArenaAllocate(arena, sizeof(T) * count, std::alignment_of<T>::value);
}

/// @summary Retrieve an exact marker that can be used to reset or decommit the arena, preserving all current allocations.
/// @param arena The memory arena to query.
/// @return The marker representing the byte offset of the next allocation.
public_function inline os_arena_marker_t
OsHostMemoryArenaMark
(
    OS_HOST_MEMORY_ARENA *arena
)
{
    return OsArenaMark(&arena->Allocator);
}

/// @summary Resets the state of the arena back to a marker, without decommitting any memory.
/// @param arena The memory arena to reset.
/// @param arena_marker The marker value returned by OsHostMemoryArenaMark().
public_function inline void
OsHostMemoryArenaResetToMarker
(
    OS_HOST_MEMORY_ARENA    *arena,
    os_arena_marker_t arena_marker
)
{
    OsArenaResetToMarker(&arena->Allocator, arena_marker);
}

/// @summary Resets the state of the arena to empty, without decomitting any memory.
/// @param arena The memory arena to reset.
public_function inline void
OsHostMemoryArenaReset
(
    OS_HOST_MEMORY_ARENA *arena
)
{
    OsArenaReset(&arena->Allocator);
}

/// @summary Retrieve a high-resolution timestamp value.
/// @return A high-resolution timestamp. The timestamp is specified in counts per-second.
public_function uint64_t
OsTimestampInTicks
(
    void
)
{
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    return (uint64_t) ticks.QuadPart;
}

/// @summary Retrieve a nanosecond-resolution timestamp value.
/// @return The current timestamp value, in nanoseconds.
public_function uint64_t
OsTimestampInNanoseconds
(
    void
)
{
    LARGE_INTEGER freq;
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    QueryPerformanceFrequency(&freq);
    // scale the tick value by the nanoseconds-per-second multiplier
    // before scaling back down by ticks-per-second to avoid loss of precision.
    return (1000000000ULL * uint64_t(ticks.QuadPart)) / uint64_t(freq.QuadPart);
}

/// @summary Calculates the number of whole nanoseconds in a fixed slice of a whole second.
/// @param fraction The fraction of a second. For example, to calculate the number of nanoseconds in 1/60 of a second, specify 60.
/// @return The number of nanoseconds in the specified fraction of a second.
public_function uint64_t
OsNanosecondSliceOfSecond
(
    uint64_t fraction
)
{
    return 1000000000ULL / fraction;
}

/// @summary Given two timestamp values, calculate the number of nanoseconds between them.
/// @param start_ticks The TimestampInTicks at the beginning of the measured interval.
/// @param end_ticks The TimestampInTicks at the end of the measured interval.
/// @return The elapsed time between the timestamps, specified in nanoseconds.
public_function uint64_t
OsElapsedNanoseconds
(
    uint64_t start_ticks, 
    uint64_t   end_ticks
)
{   
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    // scale the tick value by the nanoseconds-per-second multiplier
    // before scaling back down by ticks-per-second to avoid loss of precision.
    return (1000000000ULL * (end_ticks - start_ticks)) / uint64_t(freq.QuadPart);
}

/// @summary Convert a time value specified in milliseconds to nanoseconds.
/// @param milliseconds The time value, in milliseconds.
/// @return The input time value, converted to nanoseconds.
public_function uint64_t
OsMillisecondsToNanoseconds
(
    uint32_t milliseconds
)
{
    return uint64_t(milliseconds) * 1000000ULL;
}

/// @summary Convert a time value specified in nanoseconds to whole milliseconds. Fractional nanoseconds are truncated.
/// @param nanoseconds The time value, in nanoseconds.
/// @return The number of whole milliseconds in the input time value.
public_function uint32_t
OsNanosecondsToWholeMilliseconds
(
    uint64_t nanoseconds
)
{
    return (uint32_t)(nanoseconds / 1000000ULL);
}

/// @summary Enumerate all CPU resources of the host system.
/// @param cpu_info The structure to populate with information about host CPU resources.
/// @param scratch_mem Temporary scratch memory to use while enumerating CPU resources.
/// @return true if the host CPU information was successfully retrieved.
public_function bool
OsQueryHostCpuLayout
(
    OS_CPU_INFO       *cpu_info, 
    OS_MEMORY_RANGE scratch_mem
)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *lpibuf = NULL;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info   = NULL;
    OS_HOST_MEMORY_ARENA arena = {};
    size_t           alignment = OsAlignmentOfType<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>();
    size_t           smt_count = 0;
    uint8_t           *bufferp = NULL;
    uint8_t           *buffere = NULL;
    DWORD          buffer_size = 0;
    int                regs[4] ={0, 0, 0, 0};

    // zero out the CPU information returned to the caller.
    ZeroMemory(cpu_info, sizeof(OS_CPU_INFO));
    
    // retrieve the CPU vendor string using the __cpuid intrinsic.
    __cpuid(regs  , 0); // CPUID function 0
    *((int*)&cpu_info->VendorName[0]) = regs[1]; // EBX
    *((int*)&cpu_info->VendorName[4]) = regs[3]; // ECX
    *((int*)&cpu_info->VendorName[8]) = regs[2]; // EDX
         if (!strcmp(cpu_info->VendorName, "AuthenticAMD")) cpu_info->PreferAMD        = true;
    else if (!strcmp(cpu_info->VendorName, "GenuineIntel")) cpu_info->PreferIntel      = true;
    else if (!strcmp(cpu_info->VendorName, "KVMKVMKVMKVM")) cpu_info->IsVirtualMachine = true;
    else if (!strcmp(cpu_info->VendorName, "Microsoft Hv")) cpu_info->IsVirtualMachine = true;
    else if (!strcmp(cpu_info->VendorName, "VMwareVMware")) cpu_info->IsVirtualMachine = true;
    else if (!strcmp(cpu_info->VendorName, "XenVMMXenVMM")) cpu_info->IsVirtualMachine = true;

    // figure out the amount of space required, and allocate a temporary buffer:
    GetLogicalProcessorInformationEx(RelationAll, NULL, &buffer_size);
    if (scratch_mem.SizeInBytes < (buffer_size + (alignment - 1)))
    {
        OsLayerError("ERROR: %S: Insufficient memory to query host CPU layout.\n", __FUNCTION__);
        cpu_info->NumaNodes       = 1;
        cpu_info->PhysicalCPUs    = 1;
        cpu_info->PhysicalCores   = 1;
        cpu_info->HardwareThreads = 1;
        cpu_info->ThreadsPerCore  = 1;
        return false;
    }
    OsCreateHostMemoryArena(&arena, scratch_mem);
    lpibuf = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) OsHostMemoryArenaAllocate(arena, buffer_size, alignment);
    GetLogicalProcessorInformationEx(RelationAll, lpibuf, &buffer_size);

    // initialize the output counts:
    cpu_info->NumaNodes       = 0;
    cpu_info->PhysicalCPUs    = 0;
    cpu_info->PhysicalCores   = 0;
    cpu_info->HardwareThreads = 0;
    cpu_info->ThreadsPerCore  = 0;

    // step through the buffer and update counts:
    bufferp = (uint8_t*) lpibuf;
    buffere =((uint8_t*) lpibuf) + size_t(buffer_size);
    while (bufferp < buffere)
    {
        info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) bufferp;
        switch (info->Relationship)
        {
            case RelationNumaNode:
                { cpu_info->NumaNodes++;
                } break;

            case RelationProcessorPackage:
                { cpu_info->PhysicalCPUs++;
                } break;

            case RelationProcessorCore:
                { cpu_info->PhysicalCores++;
                  if (info->Processor.Flags == LTP_PC_SMT)
                      smt_count++;
                } break;

            default:
                {   // RelationGroup, RelationCache - don't care.
                } break;
        }
        bufferp += size_t(info->Size);
    }

    // determine the total number of logical processors in the system.
    // use this value to figure out the number of threads per-core.
    if (smt_count > 0)
    {   // determine the number of logical processors in the system and
        // use this value to figure out the number of threads per-core.
        SYSTEM_INFO sysinfo;
        GetNativeSystemInfo(&sysinfo);
        cpu_info->ThreadsPerCore = size_t(sysinfo.dwNumberOfProcessors) / smt_count;
    }
    else
    {   // there are no SMT-enabled CPUs in the system, so 1 thread per-core.
        cpu_info->ThreadsPerCore = 1;
    }

    // calculate the total number of available hardware threads.
    cpu_info->HardwareThreads = (smt_count * cpu_info->ThreadsPerCore) + (cpu_info->PhysicalCores - smt_count);
    return true;
}

/// @summary Retrieve the operating system identifier of the calling thread.
/// @return The operating system identifier of the calling thread.
public_function uint32_t
OsThreadId
(
    void
)
{
    return GetCurrentThreadId();
}

/// @summary Calculate the amount of memory required to create a ready-to-run task queue.
/// @param max_active_tasks The maximum number of active tasks in the owning OS_TASK_POOL. This value must be a power of two.
/// @return The number of bytes required to create an OS_TASK_QUEUE with the specified capacity.
public_function size_t
OsAllocationSizeForTaskQueue
(
    size_t max_active_tasks
)
{   // max_active_tasks must be a power-of-two.
    assert((max_active_tasks & (max_active_tasks-1)) == 0);
    return OsAllocationSizeForArray<os_task_id_t>(max_active_tasks);
}

/// @summary Calculate the amount of memory required to create an OS_TASK_POOL with the specified attributes.
/// @param init The OS_TASK_POOL_INIT describing the task pool attributes.
/// @return The number of bytes required to create a single OS_TASK_POOL with the specified attributes.
public_function size_t
OsAllocationSizeForTaskPoolType
(
    OS_TASK_POOL_INIT *init
)
{
    size_t  slot_size = OsAllocationSizeForArray<OS_TASK_POOL::atomic_u8_t>(init->MaxActiveTasks);
    size_t  data_size = OsAllocationSizeForArray<OS_TASK_DATA>(init->MaxActiveTasks);
    size_t    io_size = OsAllocationSizeForIoRequestPool(init->MaxIoRequests);
    size_t queue_size = OsAllocationSizeForTaskQueue(init->MaxActiveTasks);
    return (slot_size + data_size + queue_size + io_size);
}

/// @summary Calculate the amount of memory required to create an OS_TASK_SCHEDULER with the specified attributes.
/// @param init The OS_TASK_SCHEDULER_INIT describing the task scheduler attributes.
/// @return The number of bytes required to create an OS_TASK_SCHEDULER with the specified attributes. This value does not include the global or local memory, or per-thread stack memory.
public_function size_t
OsAllocationSizeForTaskScheduler
(
    OS_TASK_SCHEDULER_INIT *init
)
{
    OS_TASK_POOL_INIT *pool_types = init->TaskPoolTypes;
    size_t              num_bytes = 0;
    size_t             pool_count = 0;

    num_bytes += OsAllocationSizeForArray<uint32_t        >(init->PoolTypeCount);
    num_bytes += OsAllocationSizeForArray<OS_TASK_POOL*   >(init->PoolTypeCount);
    num_bytes += OsAllocationSizeForArray<CRITICAL_SECTION>(init->PoolTypeCount);
    for (size_t i = 0, n = init->PoolTypeCount; i < n; ++i)
    {
        num_bytes  += OsAllocationSizeForTaskPoolType(&pool_types[i]) * pool_types[i].PoolCount;
        pool_count += pool_types[i].PoolCount;
    }
    num_bytes += OsAllocationSizeForArray<OS_TASK_POOL        >(pool_count);
    num_bytes += OsAllocationSizeForArray<OS_HOST_MEMORY_ARENA>(pool_count);
    num_bytes += OsAllocationSizeForArray<OS_IO_REQUEST_POOL  >(pool_count);
    num_bytes += OsAllocationSizeForArray<unsigned int>(init->WorkerThreadCount);
    num_bytes += OsAllocationSizeForArray<HANDLE      >(init->WorkerThreadCount);
    num_bytes += OsAllocationSizeForArray<HANDLE      >(init->WorkerThreadCount);
    num_bytes += OsAllocationSizeForArray<HANDLE      >(init->WorkerThreadCount);
    num_bytes += OsAllocationSizeForArray<HANDLE      >(init->WorkerThreadCount);
    return num_bytes;
}

/// @summary Create a task ID from its constituent parts.
/// @param type One of the values of the OS_TASK_ID_TYPE enumeration specifying whether the task is an internal or external task.
/// @param pool The zero-based index of the OS_TASK_POOL that is creating the task ID.
/// @param index The zero-based index of the task within the OS_TASK_POOL storage.
/// @param valid One of the values of the OS_TASK_ID_VALIDITY enumeration specifying whether the task ID indicates a valid task.
/// @return The task identifier.
public_function inline os_task_id_t
OsMakeTaskId
(
    uint32_t type, 
    uint32_t pool, 
    uint32_t index, 
    uint32_t valid=OS_TASK_ID_VALID
)
{
    return ((valid & 0x0001) << OS_TASK_ID_SHIFT_VALID) | 
           ((type  & 0x0001) << OS_TASK_ID_SHIFT_TYPE ) |
           ((pool  & 0x0FFF) << OS_TASK_ID_SHIFT_POOL ) | 
           ((index & 0xFFFF) << OS_TASK_ID_SHIFT_INDEX);
}

/// @summary Determine whether an ID identifies a valid task.
/// @param task_id The task identifier.
/// @return true if the identifier specifies a valid task.
public_function inline bool
OsIsValidTask
(
    os_task_id_t task_id
)
{
    return ((task_id & OS_TASK_ID_MASK_VALID) != 0);
}

/// @summary Determine whether a task ID identifies an external task.
/// @param task_id The task identifier.
/// @return true if the identifier specifies an external task.
public_function inline bool
OsIsExternalTask
(
    os_task_id_t task_id
)
{
    return ((task_id & OS_TASK_ID_MASK_TYPE) == 0);
}

/// @summary Determine whether a task ID identifies an internal task.
/// @param task_id The task identifier.
/// @return true if the identifier specifies an internal task.
public_function inline bool
OsIsInternalTask
(
    os_task_id_t task_id
)
{
    return ((task_id & OS_TASK_ID_MASK_TYPE) != 0);
}

/// @summary Implement the internal entry point of a task scheduler worker thread.
/// @param argp Pointer to an OS_TASK_SCHEDULER_THREAD_INIT instance specific to this thread.
/// @return Zero if the thread terminated normally, or non-zero for abnormal termination.
public_function unsigned int __cdecl
OsTaskSchedulerThreadMain
(
    void *argp
)
{
    OS_TASK_SCHEDULER_THREAD_INIT  init = {};
    OS_TASK_ENVIRONMENT         taskenv = {};
    OS_TASK_POOL                *victim = NULL;
    HANDLE                         iocp = NULL;
    OVERLAPPED              *overlapped = NULL;
    uintptr_t                signal_arg = 0;
    DWORD                     num_bytes = 0;
    DWORD                           tid = GetCurrentThreadId();
    os_task_id_t              work_item = OS_INVALID_TASK_ID;
    unsigned int              exit_code = 1;
    bool                   keep_running = true;
    bool                      more_work = false;

    // copy the initialization data into local stack memory.
    // argp may have been allocated on the stack of the caller 
    // and is only guaranteed to remain valid until the ReadySignal is set.
    CopyMemory(&init, argp, sizeof(OS_TASK_SCHEDULER_THREAD_INIT));
    iocp = init.CompletionPort;

    // spit out a message just prior to initialization:
    OsLayerOutput("START: %S(%u): Task scheduler worker thread starting.\n", __FUNCTION__, tid);

    // allocate the task pool and bind it to the worker thread for the duration of the thread's execution.
    if (OsAllocateTaskPool(&taskenv, init.TaskScheduler, init.PoolId, tid) < 0)
    {
        OsLayerError("ERROR: %S(%u): Task scheduler worker failed to allocate task pool.\n", __FUNCTION__, tid);
        OsLayerError("DEATH: %S(%u): Task scheduler worker terminating.\n", __FUNCTION__, tid);
        SetEvent(init.ErrorSignal);
        return 1;
    }

    // signal the main thread that this thread is ready to run.
    SetEvent(init.ReadySignal);

    __try
    {
        while (keep_running)
        {   // enter a wait on the completion port. the thread will receive a notification 
            // when it has been assigned some work to steal (or to shut down), and wake up.
            if (GetQueuedCompletionStatus(iocp, &num_bytes, &signal_arg, &overlapped, INFINITE))
            {   // did this thread receive a shutdown or steal notification?
                if (signal_arg == OS_COMPLETION_KEY_SHUTDOWN)
                {   // the task scheduler is being shut down gracefully.
                    keep_running = false;
                    exit_code = 0;
                    break;
                }
                else
                {   // the completion key is the OS_TASK_POOL to steal from.
                    // num_bytes is set to the number of tasks to steal (for now, always 1.)
                    victim = (OS_TASK_POOL*) signal_arg;
                }
                // loop for as long as we can get work. the thread went to sleep because 
                // its local task queue was empty, and woke up because another thread sent
                // a notification that it has some work available to steal, so first attempt
                // to steal a task from the victim task pool. if successful, execute the 
                // stolen task, which may produce additional work in the local task queue.
                // continue to execute work from the local task queue until it is empty.
                for ( ; ; )
                {   // first attempt to steal a task from the victim task pool that woke us.
                    for (size_t steal_attempts = 0; steal_attempts < 4; ++steal_attempts)
                    {   // due to queue contention, a steal attempt may fail even though 
                        // there's still a task available in the victim's ready-to-run queue.
                        if ((work_item = OsTaskQueueSteal(&victim->WorkQueue, more_work)) != OS_INVALID_TASK_ID)
                            break;
                    }
                    if (work_item == OS_INVALID_TASK_ID)
                    {   // no work could be stolen from the victim's work queue, so this time
                        // select another victim task pool to steal from - we might get lucky.
                        // since the thread is already awake, try as hard as possible to get work 
                        // before putting the thread back to sleep - context switches are expensive.
                        size_t      steal_index = taskenv.TaskPool->PoolIndex;
                        size_t      start_index = taskenv.TaskPool->PoolIndex;
                        OS_TASK_POOL *pool_list = taskenv.TaskPool->TaskPoolList;
                        size_t       pool_count = taskenv.TaskScheduler->TaskPoolCount;
                        do
                        {   // execute a single attempt to steal from the next pool in the list.
                            steal_index = (steal_index + 1) % pool_count;
                            if ((work_item = OsTaskQueueSteal(&pool_list[steal_index].WorkQueue, more_work)) != OS_INVALID_TASK_ID)
                                break; // break out of do...while.
                        } while (steal_index != start_index);

                        if (work_item == OS_INVALID_TASK_ID)
                        {   // all attempts to steal work failed. go back to sleep 
                            // unless there's something waiting on the completion port.
                            break; // break out of for ( ; ; )
                        }
                    }
                    // at this point, a valid work item has been stolen from some victim.
                    // begin the main task execution loop, executing a work item and then
                    // taking from the local ready-to-run queue for as long as possible.
                    do
                    {   // execute a single task, which may produce additional tasks in the thread-local ready-to-run queue.
                        uint32_t const tsrc = (work_item & OS_TASK_ID_MASK_POOL) >> OS_TASK_ID_SHIFT_POOL;
                        uint32_t const tidx = (work_item & OS_TASK_ID_MASK_INDEX) >> OS_TASK_ID_SHIFT_INDEX;
                        OS_TASK_DATA  *task = &taskenv.TaskPool->TaskPoolList[tsrc].TaskPoolData[tidx];

                        // set up the work environment and execute the task.
                        OsHostMemoryArenaReset(taskenv.LocalMemory);
                        task->TaskMain(work_item, task->TaskData, &taskenv);
                        OsCompleteTask(&taskenv, work_item);

                        // and then attempt to grab another task from the thread-local ready-to-run queue.
                    } while ((work_item = OsTaskQueueTake(&taskenv.TaskPool->WorkQueue, more_work)) != OS_INVALID_TASK_ID);
                }
            }
            // reset the wake signal to 0/NULL for the next iteration.
            signal_arg = 0;
        }
    }
    __finally
    {   // the worker is terminating - clean up thread-local resources.
        // spit out a message just prior to termination.
        OsLayerOutput("DEATH: %S(%u): Task scheduler worker terminating.\n", __FUNCTION__, tid);
        return exit_code;
    }
}

/// @summary Create a new task scheduler instance. The calling thread is blocked until all worker threads are initialized.
/// @param scheduler The OS_TASK_SCHEDULER to initialize.
/// @param init An OS_TASK_SCHEDULER_INIT structure describing the task scheduler configuration.
/// @return Zero if the task scheduler is successfully initialized, or -1 if an error occurred.
public_function int
OsCreateTaskScheduler
(
    OS_TASK_SCHEDULER *scheduler, 
    OS_TASK_SCHEDULER_INIT *init, 
    char const             *name
)
{
    OS_HOST_MEMORY_ALLOCATION  *memory = NULL;
    uint32_t                  *id_list = NULL;
    OS_TASK_POOL          **free_lists = NULL;
    CRITICAL_SECTION       *list_locks = NULL;
    OS_TASK_POOL            *pool_list = NULL;
    OS_HOST_MEMORY_ARENA   *arena_list = NULL;
    OS_IO_REQUEST_POOL      *iorp_list = NULL;
    unsigned int           *thread_ids = NULL;
    HANDLE             *thread_handles = NULL;
    HANDLE               *thread_ready = NULL;
    HANDLE               *thread_error = NULL;
    HANDLE                *thread_iocp = NULL;
    CV_PROVIDER           *cv_provider = NULL;
    CV_MARKERSERIES         *cv_series = NULL;
    HRESULT                  cv_result = S_OK;
    char                   cv_name[64] = {};
    OS_HOST_MEMORY_ARENA    global_mem = {};
    OS_HOST_MEMORY_ARENA scheduler_mem = {};
    OS_MEMORY_RANGE            all_mem = {};
    OS_CPU_INFO               cpu_info = {};
    size_t              bytes_required = 0;
    size_t                thread_count = 0;
    size_t                  pool_count = 0;
    size_t                  pool_index = 0;
    size_t           worker_pool_index = 0;
    uint32_t            worker_pool_id = 0;
    bool             found_worker_pool = false;

    // initialize the fields of the OS_TASK_SCHEDULER object.
    OsZeroMemory(scheduler, sizeof(OS_TASK_SCHEDULER));

    // count the total nuber of task pools that will be created, and locate the pool to use for the worker threads.
    // only one pool type should be marked as being used for worker threads.
    for (size_t i = 0, n = init->PoolTypeCount; i < n; ++i)
    {
        if (init->TaskPoolTypes[i].PoolUsage & OS_TASK_POOL_USAGE_FLAG_WORKER)
        {
            if (found_worker_pool)
            {
                OsLayerError("ERROR: %S(%u): Multiple pool types found with OS_TASK_POOL_USAGE_FLAG_WORKER.\n", __FUNCTION__, GetCurrentThreadId());
                return -1;
            }
            else
            {
                worker_pool_id    = init->TaskPoolTypes[i].PoolId;
                worker_pool_index = i;
                found_worker_pool = true;
            }
        }
        size_t ntasks = init->TaskPoolTypes[i].MaxActiveTasks;
        if ((ntasks & (ntasks-1)) != 0)
        {   // the maximum number of active tasks must be a power of two.
            // round up to the next largest power of two.
            size_t user = ntasks;
            size_t newv = 1;
            while (newv < user)
            {
                newv <<= 1;
            }
            init->TaskPoolTypes[i].MaxActiveTasks = newv;
            OsLayerError("WARNING: %S(%u): MaxActiveTasks (%Iu) for Task Pool Id %u must be a power-of-two; rounding up to %Iu.\n", __FUNCTION__, GetCurrentThreadId(), user, init->TaskPoolTypes[i].PoolId, newv);
        }
        if (init->TaskPoolTypes[i].MaxActiveTasks < OS_MIN_TASKS_PER_POOL)
        {
            OsLayerError("WARNING: %S(%u): MaxActiveTasks (%Iu) for Task Pool Id %u increased to minimum (%u).\n", __FUNCTION__, GetCurrentThreadId(), init->TaskPoolTypes[i].MaxActiveTasks, init->TaskPoolTypes[i].PoolId, OS_MIN_TASKS_PER_POOL);
            init->TaskPoolTypes[i].MaxActiveTasks = OS_MIN_TASKS_PER_POOL;
        }
        if (init->TaskPoolTypes[i].MaxActiveTasks > OS_MAX_TASKS_PER_POOL)
        {
            OsLayerError("WARNING: %S(%u): MaxActiveTasks (%Iu) for Task Pool Id %u decreased to maximum (%u).\n", __FUNCTION__, GetCurrentThreadId(), init->TaskPoolTypes[i].MaxActiveTasks, init->TaskPoolTypes[i].PoolId, OS_MAX_TASKS_PER_POOL);
            init->TaskPoolTypes[i].MaxActiveTasks = OS_MAX_TASKS_PER_POOL;
        }
        pool_count += init->TaskPoolTypes[i].PoolCount;
    }
    if (init->WorkerThreadCount > 0 && !found_worker_pool)
    {
        OsLayerError("ERROR: %S(%u): No pool type found with OS_TASK_POOL_USAGE_FLAG_WORKER.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }
    if (pool_count == 0)
    {
        OsLayerError("ERROR: %S(%u): Cannot create scheduler with zero task pools.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }
    if (init->SchedulerMemoryPool == NULL)
    {
        OsLayerError("ERROR: %S(%u): No host memory pool provided to scheduler.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }

    // determine the total amount of memory required for all scheduler data, 
    // and acquire a single host memory allocation of at least that size.
    // bytes_required is a worst-case value; it may not be 100% used.
    size_t vmalign  = init->SchedulerMemoryPool->Granularity;
    bytes_required  = 0;
    bytes_required += OsAllocationSizeForArray<uint32_t            >(init->PoolTypeCount);     // OS_TASK_SCHEDULER::PoolIdList.
    bytes_required += OsAllocationSizeForArray<OS_TASK_POOL       *>(init->PoolTypeCount);     // OS_TASK_SCHEDULER::PoolFreeLists.
    bytes_required += OsAllocationSizeForArray<CRITICAL_SECTION    >(init->PoolTypeCount);     // OS_TASK_SCHEDULER::PoolFreeListLocks.
    bytes_required += OsAllocationSizeForArray<OS_TASK_POOL        >(pool_count);              // OS_TASK_SCHEDULER::TaskPoolList.
    bytes_required += OsAllocationSizeForArray<OS_HOST_MEMORY_ARENA>(pool_count);              // OS_TASK_SCHEDULER::TaskPoolArenas.
    bytes_required += OsAllocationSizeForArray<OS_IO_REQUEST_POOL  >(pool_count);              // OS_TASK_SCHEDULER::TaskIoRequestPools.
    bytes_required += OsAllocationSizeForArray<unsigned int        >(init->WorkerThreadCount); // OS_TASK_SCHEDULER::WorkerThreadIds.
    bytes_required += OsAllocationSizeForArray<HANDLE              >(init->WorkerThreadCount); // OS_TASK_SCHEDULER::WorkerThreadHandle.
    bytes_required += OsAllocationSizeForArray<HANDLE              >(init->WorkerThreadCount); // OS_TASK_SCHEDULER::WorkerThreadReady.
    bytes_required += OsAllocationSizeForArray<HANDLE              >(init->WorkerThreadCount); // OS_TASK_SCHEDULER::WorkerThreadError.
    bytes_required += OsAllocationSizeForArray<HANDLE              >(init->WorkerThreadCount); // OS_TASK_SCHEDULER::WorkerThreadPort.
    if (init->GlobalMemorySize > 0)
    {   // include the global memory in the total.
        // the global memory must have the same alignment as a VMM allocation (typically 64KB).
        bytes_required += init->GlobalMemorySize + (vmalign - 1);
    }
    for (size_t  i  = 0, n = init->PoolTypeCount; i < n; ++i)
    {
        size_t type_nbytes = 0;
        type_nbytes       += OsAllocationSizeForArray<OS_TASK_POOL::atomic_u8_t>(init->TaskPoolTypes[i].MaxActiveTasks); // OS_TASK_POOL::SlotStatus.
        type_nbytes       += OsAllocationSizeForArray<OS_TASK_DATA             >(init->TaskPoolTypes[i].MaxActiveTasks); // OS_TASK_POOL::TaskPoolData.
        type_nbytes       += OsAllocationSizeForArray<os_task_id_t             >(init->TaskPoolTypes[i].MaxActiveTasks); // OS_TASK_POOL::WorkQueue::TaskIds.
        if (init->TaskPoolTypes[i].MaxIoRequests > 0)
        {   // include storage for an I/O request pool in the total.
            type_nbytes   += OsAllocationSizeForArray<OS_IO_REQUEST            >(init->TaskPoolTypes[i].MaxIoRequests ); // OS_IO_REQUEST_POOL::NodePool.
        }
        if (init->TaskPoolTypes[i].LocalMemorySize > 0)
        {   // include the pool-local memory in the total.
            // the local memory must have the same alignment as a VMM allocation (typically 64KB).
            type_nbytes   += init->TaskPoolTypes[i].LocalMemorySize + (vmalign - 1);
        }
        bytes_required    += type_nbytes * init->TaskPoolTypes[i].PoolCount;
    }

    // acquire a single contiguous memory allocation from the pool.
    if ((memory = OsHostMemoryPoolAllocate(init->SchedulerMemoryPool, bytes_required, bytes_required, OS_HOST_MEMORY_ALLOCATION_FLAGS_READWRITE)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Failed to allocate %Iu bytes of host memory for task scheduler.\n", __FUNCTION__, GetCurrentThreadId(), bytes_required);
        goto cleanup_and_fail;
    }
    // initialize an arena allocator so we can sub-allocate from the main host memory allocation.
    if (OsCreateHostMemoryArena(&scheduler_mem, OsInitHostMemoryRange(memory)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Failed to initialize host memory arena for task scheduler.\n", __FUNCTION__, GetCurrentThreadId());
        goto cleanup_and_fail;
    }

    // query the host CPU layout and initialize the Concurrency Visualizer objects.
    if (!OsQueryHostCpuLayout(&cpu_info, OsInitHostMemoryRange(memory)))
    {
        OsLayerError("ERROR: %S(%u): Unable to query the layout of the host CPU.\n", __FUNCTION__, GetCurrentThreadId());
        goto cleanup_and_fail;
    }
    if (name == NULL)
    {   // Concurrency Visualizer SDK requires a non-NULL string.
        sprintf_s(cv_name, "Unnamed task scheduler 0x%p", scheduler);
    }
    if (!SUCCEEDED((cv_result = CvInitProvider(&TaskProfilerGUID, &cv_provider))))
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize task profiler provider (HRESULT 0x%08X).\n", __FUNCTION__, GetCurrentThreadId(), cv_result);
        goto cleanup_and_fail;
    }
    if (!SUCCEEDED((cv_result = CvCreateMarkerSeriesA(cv_provider, name, &cv_series))))
    {
        OsLayerError("ERROR: %S(%u): Unable to create task profiler marker series (HRESULT 0x%08X).\n", __FUNCTION__, GetCurrentThreadId(), cv_result);
        goto cleanup_and_fail;
    }

    // allocate the scheduler global memory, shared between all workers, and 
    // the initialize a memory arena to sub-allocate from it.
    if (init->GlobalMemorySize > 0)
    {
        void *gmem = OsHostMemoryArenaAllocate(&scheduler_mem, init->GlobalMemorySize, vmalign);
        if   (gmem == NULL)
        {
            OsLayerError("ERROR: %S(%u): Failed to allocate global memory of %Iu bytes with alignment %Iu.\n", __FUNCTION__, GetCurrentThreadId(), init->GlobalMemorySize, vmalign);
            goto cleanup_and_fail;
        }
        if (OsCreateHostMemoryArena(&global_mem, OsInitHostMemoryRange(gmem, init->GlobalMemorySize)) < 0)
        {
            OsLayerError("ERROR: %S(%u): Failed to initialize global memory arena.\n", __FUNCTION__, GetCurrentThreadId());
            goto cleanup_and_fail;
        }
    }

    // allocate memory for the various scheduler lists.
    id_list      = OsHostMemoryArenaAllocateArray<uint32_t          >(&scheduler_mem, init->PoolTypeCount);
    free_lists   = OsHostMemoryArenaAllocateArray<OS_TASK_POOL*     >(&scheduler_mem, init->PoolTypeCount);
    list_locks   = OsHostMemoryArenaAllocateArray<CRITICAL_SECTION  >(&scheduler_mem, init->PoolTypeCount);
    pool_list    = OsHostMemoryArenaAllocateArray<OS_TASK_POOL      >(&scheduler_mem, pool_count);
    arena_list   = OsHostMemoryArenaAllocateArray<OS_MEMORY_ARENA   >(&scheduler_mem, pool_count);
    iorp_list    = OsHostMemoryArenaAllocateArray<OS_IO_REQUEST_POOL>(&scheduler_mem, pool_count);
    if (id_list == NULL || free_lists == NULL || list_locks == NULL || pool_list == NULL || arena_list == NULL || iorp_list == NULL)
    {
        OsLayerError("ERROR: %S(%u): Failed to allocate memory for task scheduler.\n", __FUNCTION__, GetCurrentThreadId());
        goto cleanup_and_fail;
    }
    ZeroMemory(id_list   , init->PoolTypeCount * sizeof(uint32_t));
    ZeroMemory(free_lists, init->PoolTypeCount * sizeof(OS_TASK_POOL*));
    ZeroMemory(list_locks, init->PoolTypeCount * sizeof(CRITICAL_SECTION));
    ZeroMemory(pool_list , pool_count          * sizeof(OS_TASK_POOL));
    ZeroMemory(arena_list, pool_count          * sizeof(OS_MEMORY_ARENA));
    ZeroMemory(iorp_list , pool_count          * sizeof(OS_IO_REQUEST_POOL));

    // allocate memory for the worker thread pool.
    if (init->WorkerThreadCount > 0)
    {
        thread_ids      = OsHostMemoryArenaAllocateArray<unsigned int>(&scheduler_mem, init->WorkerThreadCount);
        thread_handles  = OsHostMemoryArenaAllocateArray<HANDLE>(&scheduler_mem, init->WorkerThreadCount);
        thread_ready    = OsHostMemoryArenaAllocateArray<HANDLE>(&scheduler_mem, init->WorkerThreadCount);
        thread_error    = OsHostMemoryArenaAllocateArray<HANDLE>(&scheduler_mem, init->WorkerThreadCount);
        thread_iocp     = OsHostMemoryArenaAllocateArray<HANDLE>(&scheduler_mem, init->WorkerThreadCount);
        if (thread_ids == NULL || thread_handles == NULL || thread_ready == NULL || thread_error == NULL || thread_iocp == NULL)
        {
            OsLayerError("ERROR: %S(%u): Failed to allocate memory for task scheduler thread pool.\n", __FUNCTION__, GetCurrentThreadId());
            goto cleanup_and_fail;
        }
        ZeroMemory(thread_ids    , init->WorkerThreadCount * sizeof(unsigned int));
        ZeroMemory(thread_handles, init->WorkerThreadCount * sizeof(HANDLE));
        ZeroMemory(thread_ready  , init->WorkerThreadCount * sizeof(HANDLE));
        ZeroMemory(thread_error  , init->WorkerThreadCount * sizeof(HANDLE));
        ZeroMemory(thread_iocp   , init->WorkerThreadCount * sizeof(HANDLE));
    }

    // initialize all of the task pools and the associated free lists.
    for (size_t type_idx = 0, ntypes = init->PoolTypeCount; type_idx < ntypes; ++type_idx)
    {
        OS_TASK_POOL_INIT  &pool_def = init->TaskPoolTypes[type_idx];
        id_list[type_idx] = pool_def.PoolId;
        InitializeCriticalSectionAndSpinCount(&list_locks[type_idx], 0x1000);
        for (size_t pool_idx = 0, npools = pool_def.PoolCount; pool_idx < npools; ++pool_idx)
        {
            OS_TASK_POOL *pool    = &pool_list[pool_index];
            pool->SlotStatus      = OsHostMemoryArenaAllocateArray<OS_TASK_POOL::atomic_u8_t>(&scheduler_mem, pool_def.MaxActiveTasks);
            pool->IndexMask       =(uint32_t) (pool_def.MaxActiveTasks - 1);
            pool->NextIndex       = 0;
            pool->PoolIndex       =(uint32_t)  pool_index;
            pool->PoolUsage       = pool_def.PoolUsage;
            pool->ThreadId        = 0;
            pool->LastError       = OS_TASK_POOL_ERROR_NONE;
            pool->PoolId          = pool_def.PoolId;
            pool->NextWorker      = 0;
            pool->WorkerCount     =(uint16_t)  init->WorkerThreadCount;
            pool->TaskPoolList    = pool_list;
            pool->TaskPoolData    = OsHostMemoryArenaAllocateArray<OS_TASK_DATA>(&scheduler_mem, pool_def.MaxActiveTasks);
            pool->NextFreePool    = free_lists[type_idx];
            free_lists[type_idx]  = pool;
            if (pool->SlotStatus == NULL || pool->TaskPoolData == NULL)
            {
                OsLayerError("ERROR: %S(%u): Failed to allocate task pool memory.\n", __FUNCTION__, GetCurrentThreadId());
                goto cleanup_and_fail;
            }
            if (pool_def.MaxIoRequests > 0 && OsCreateIoRequestPool(&iorp_list[pool_index], &scheduler_mem, pool_def.MaxIoRequests) < 0)
            {
                OsLayerError("ERROR: %S(%u): Failed to allocate I/O request pool for task pool.\n", __FUNCTION__, GetCurrentThreadId());
                goto cleanup_and_fail;
            }
            if (OsCreateTaskQueue(&pool->WorkQueue, pool_def.MaxActiveTasks, &scheduler_mem) < 0)
            {
                OsLayerError("ERROR: %S(%u): Failed to allocate task pool work queue.\n", __FUNCTION__, GetCurrentThreadId());
                goto cleanup_and_fail;
            }
            if (pool_def.LocalMemorySize > 0)
            {   // allocate pool-local memory and initialize a memory arena.
                void *lmem  = OsHostMemoryAllocate(&scheduler_mem, pool_def.LocalMemorySize, vmalign);
                if   (lmem == NULL)
                {
                    OsLayerError("ERROR: %S(%u): Failed to allocate %Iu bytes of thread-local memory with alignment %Iu for task pool.\n", __FUNCTION__, GetCurrentThreadId(), pool_def.LocalMemorySize, vmalign);
                    goto cleanup_and_fail;
                }
                if (OsCreateHostMemoryArena(&arena_list[pool_index], OsInitHostMemoryRange(lmem, pool_def.LocalMemorySize)) < 0)
                {
                    OsLayerError("ERROR: %S(%u): Failed to initialize local memory arena task pool.\n", __FUNCTION__, GetCurrentThreadId(), pool_def.LocalMemorySize, vmalign);
                    goto cleanup_and_fail;
                }
            }
            ZeroMemory(pool->SlotStatus  , pool_def.MaxActiveTasks * sizeof(OS_TASK_POOL::atomic_u8_t));
            ZeroMemory(pool->TaskPoolData, pool_def.MaxActiveTasks * sizeof(OS_TASK_DATA));
            pool_index++;
        }
    }

    // initialize the fields of the OS_TASK_SCHEDULER structure.
    scheduler->PoolTypeCount             = init->PoolTypeCount;
    scheduler->PoolIdList                = id_list;
    scheduler->PoolFreeLists             = free_lists;
    scheduler->PoolFreeListLocks         = list_locks;
    scheduler->TaskPoolCount             = pool_count;
    scheduler->TaskPoolList              = pool_list;
    scheduler->TaskPoolArenas            = arena_list;
    scheduler->TaskIoRequestPools        = iorp_list;
    scheduler->WorkerThreadCount         = init->WorkerThreadCount;
    scheduler->WorkerThreadIds           = thread_ids;
    scheduler->WorkerThreadHandle        = thread_handles;
    scheduler->WorkerThreadReady         = thread_ready;
    scheduler->WorkerThreadError         = thread_error;
    scheduler->WorkerThreadPort          = thread_iocp;
    scheduler->GlobalMemoryArena         = global_mem;
    scheduler->IoThreadPool              = init->IoThreadPool;
    scheduler->HostCpuInfo               = cpu_info;
    scheduler->TaskContextData           = init->TaskContextData;
    scheduler->TaskProfiler.Provider     = cv_provider;
    scheduler->TaskProfiler.MarkerSeries = cv_series;
    scheduler->SchedulerArena            = scheduler_mem;
    scheduler->SchedulerMemory           = memory;
    scheduler->SchedulerMemoryPool       = init->SchedulerMemoryPool;

    // set up the worker init structure and spawn all worker threads in the pool.
    for (size_t thread_idx = 0, nthreads = init->WorkerThreadCount; thread_idx < nthreads; ++thread_idx)
    {
        OS_TASK_SCHEDULER_THREAD_INIT winit = {};
        const DWORD            THREAD_READY = 0;
        const DWORD            THREAD_ERROR = 1;
        const DWORD              WAIT_COUNT = 2;
        HANDLE              wset[WAIT_COUNT]={};
        DWORD                        waitrc = 0;

        // create the manual-reset events signaled by the worker to indicate that it is ready.
        if ((thread_ready[thread_idx] = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to create ready signal for worker %Iu of %Iu (%08X).\n", __FUNCTION__, GetCurrentThreadId(), thread_idx, nthreads, GetLastError());
            goto cleanup_and_fail;
        }
        if ((thread_error[thread_idx] = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to create error signal for worker %Iu of %Iu (%08X).\n", __FUNCTION__, GetCurrentThreadId(), thread_idx, nthreads, GetLastError());
            CloseHandle(thread_ready[thread_idx]); thread_ready[thread_idx] = NULL;
            goto cleanup_and_fail;
        }
        if ((thread_iocp[thread_idx] = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to create I/O completion port for worker %Iu of %Iu (%08X).\n", __FUNCTION__, GetCurrentThreadId(), thread_idx, nthreads, GetLastError());
            CloseHandle(thread_error[thread_idx]); thread_error[thread_idx] = NULL;
            CloseHandle(thread_ready[thread_idx]); thread_ready[thread_idx] = NULL;
            goto cleanup_and_fail;
        }

        // populate the OS_TASK_SCHEDULER_THREAD_INIT and then spawn the worker thread.
        // the worker thread will need to copy this structure if it wants to access it 
        // past the point where it signals the wready event.
        winit.TaskScheduler   = scheduler;
        winit.HostCpuInfo     = cpu_info;
        winit.CompletionPort  = thread_iocp [thread_idx];
        winit.ReadySignal     = thread_ready[thread_idx];
        winit.ErrorSignal     = thread_error[thread_idx];
        winit.TaskContextData = init->TaskContextData;
        winit.IoThreadPool    = init->IoThreadPool;
        winit.WorkerIndex     =(uint32_t) thread_idx;
        winit.PoolId          = worker_pool_id;
        if ((thread_handles[thread_idx] = (HANDLE) _beginthreadex(NULL, 0, OsTaskSchedulerThreadMain, &winit, 0, &thread_ids[thread_idx])) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to spawn worker %Iu of %Iu (errno = %d).\n", __FUNCTION__, GetCurrentThreadId(), thread_idx, nthreads, errno);
            CloseHandle(thread_error[thread_idx]); thread_error[thread_idx] = NULL;
            CloseHandle(thread_ready[thread_idx]); thread_ready[thread_idx] = NULL;
            CloseHandle(thread_iocp [thread_idx]); thread_iocp [thread_idx] = NULL;
            goto cleanup_and_fail;
        }

        // wait for the thread to become ready.
        wset[THREAD_READY] = thread_ready[thread_idx]; 
        wset[THREAD_ERROR] = thread_error[thread_idx];
        if ((waitrc = WaitForMultipleObjects(WAIT_COUNT, wset, FALSE, INFINITE)) != (WAIT_OBJECT_0+THREAD_READY))
        {   // thread initialization failed, or the wait failed.
            // events are already in the OS_THREAD_POOL arrays, so don't clean up here.
            OsLayerError("ERROR: %S(%u): Failed to initialize worker %Iu of %Iu (%08X).\n", __FUNCTION__, GetCurrentThreadId(), thread_idx, nthreads, waitrc);
            goto cleanup_and_fail;
        }
        // SetThreadGroupAffinity, eventually.

        // increment the number of threads successfully launched.
        thread_count++;
    }

    return 0;

cleanup_and_fail:
    if (thread_count > 0)
    {   // signal all threads to terminate, and then wait until they all die.
        // all workers are blocked waiting on their I/O completion port.
        for (size_t i = 0, n = thread_count; i < n; ++i)
        {
            PostQueuedCompletionStatus(thread_iocp[i], 0, OS_COMPLETION_KEY_SHUTDOWN, NULL);
        }
        WaitForMultipleObjects((DWORD) thread_count, thread_handles, TRUE, INFINITE);
        // now that all threads have exited, close their handles.
        for (size_t i = 0, n = thread_count; i < n; ++i)
        {
            CloseHandle(thread_handles[i]);
            CloseHandle(thread_error[i]);
            CloseHandle(thread_ready[i]);
            CloseHandle(thread_iocp[i]);
        }
    }
    if (list_locks != NULL)
    {   // delete all of the pool type free list critical sections.
        for (size_t i = 0, n = init->PoolTypeCount; i < n; ++i)
        {
            DeleteCriticalSection(&list_locks[i]);
        }
    }
    // clean up the task profiler objects.
    if (cv_series) CvReleaseMarkerSeries(cv_series);
    if (cv_provider) CvReleaseProvider(cv_provider);
    // reset the state of the memory arena.
    if (memory != NULL)
    {   // return all allocated memory back to the source pool.
        OsHostMemoryPoolRelease(init->SchedulerMemoryPool, memory);
    }
    // reset all fields of the OS_TASK_SCHEDULER instance.
    ZeroMemory(scheduler, sizeof(OS_TASK_SCHEDULER));
    return -1;
}

/// @summary Destroy a task scheduler. All worker threads are terminated. The calling thread is blocked until all worker threads exit.
/// @param scheduler The OS_TASK_SCHEDULER to destroy.
public_function void
OsDestroyTaskScheduler
(
    OS_TASK_SCHEDULER *scheduler
)
{
    if (scheduler->WorkerThreadCount > 0)
    {   // notify all threads to shut down. they will empty their local work queue first. 
        DWORD  num_threads = 0;
        for (size_t i = 0, n = scheduler->WorkerThreadCount; i < n; ++i)
        {
            if (scheduler->WorkerThreadPort[i] != NULL)
            {
                PostQueuedCompletionStatus(scheduler->WorkerThreadPort[i], 0, OS_COMPLETION_KEY_SHUTDOWN, NULL);
                num_threads++;
            }
        }
        if (num_threads > 0)
        {   // wait until all threads terminate. this may take some time.
            WaitForMultipleObjects(num_threads, scheduler->WorkerThreadHandle, TRUE, INFINITE);
        }
        // now that all threads have terminated, close their handles.
        for (size_t i = 0, n = num_threads; i < n; ++i)
        {
            CloseHandle(scheduler->WorkerThreadHandle[i]);
            CloseHandle(scheduler->WorkerThreadPort[i]);
            CloseHandle(scheduler->WorkerThreadError[i]);
            CloseHandle(scheduler->WorkerThreadReady[i]);
        }
    }
    if (scheduler->PoolTypeCount > 0)
    {   // delete all of the task pool free list critical sections.
        for (size_t i = 0, n = scheduler->PoolTypeCount; i < n; ++i)
        {
            DeleteCriticalSection(&scheduler->PoolFreeListLocks[i]);
        }
    }
    if (scheduler->TaskProfiler.MarkerSeries != NULL)
    {
        CvReleaseMarkerSeries(scheduler->TaskProfiler.MarkerSeries);
        CvReleaseProvider(scheduler->TaskProfiler.Provider);
    }
    if (scheduler->SchedulerMemory != NULL)
    {   // release the memory back to the pool.
        OsHostMemoryPoolRelease(scheduler->SchedulerMemoryPool, scheduler->SchedulerMemory);
    }
    ZeroMemory(scheduler, sizeof(OS_TASK_SCHEDULER));
}

/// @summary Allocate a task pool and bind it to a thread.
/// @param taskenv The OS_TASK_ENVIRONMENT to initialize with the allocated pool.
/// @param scheduler The OS_TASK_SCHEDULER from which the pool will be allocated.
/// @param pool_type The application pool identifier of the pool type to allocate.
/// @param thread_id The operating system identifier of the thread that will own the allocated pool.
/// @return Zero if the pool is allocated successfully, or non-zero if an error occurred.
public_function int
OsAllocateTaskPool
(
    OS_TASK_ENVIRONMENT *taskenv, 
    OS_TASK_SCHEDULER *scheduler, 
    uint32_t           pool_type, 
    uint32_t           thread_id
)
{   // locate the pool_type in the list of pool types defined on the scheduler.
    uint32_t const *pool_ids = scheduler->PoolIdList;
    size_t   pool_type_index = 0;
    bool     pool_type_found = false;
    for (size_t i = 0, n = scheduler->PoolTypeCount; i < n; ++i)
    {
        if (pool_ids[i] == pool_type)
        {
            pool_type_found = true;
            pool_type_index = i;
            break;
        }
    }
    if (pool_type_found)
    {   // attempt to pop a task pool from the free list.
        OS_TASK_POOL *pool = NULL;
        EnterCriticalSection(&scheduler->PoolFreeListLocks[pool_type_index]);
        {
            if (scheduler->PoolFreeLists[pool_type_index] != NULL)
            {
                pool = scheduler->PoolFreeLists[pool_type_index];
                scheduler->PoolFreeLists[pool_type_index] = pool->NextFreePool;
            }
        }
        LeaveCriticalSection(&scheduler->PoolFreeListLocks[pool_type_index]);
        if (pool != NULL)
        {   // the pool was successfully allocated; bind it to the thread.
            pool->NextIndex        = 0;
            pool->ThreadId         = thread_id;
            pool->LastError        = OS_TASK_POOL_ERROR_NONE;
            pool->NextWorker       = 0;
            pool->NextFreePool     = NULL;
            // initialize the task execution environment for the caller.
            taskenv->TaskProfiler  =&scheduler->TaskProfiler;
            taskenv->TaskScheduler = scheduler;
            taskenv->TaskPool      = pool;
            taskenv->HostCpuInfo   =&scheduler->HostCpuInfo;
            taskenv->ThreadId      = thread_id;
            taskenv->PoolUsage     = pool->PoolUsage;
            taskenv->ContextData   = scheduler->TaskContextData;
            taskenv->LocalMemory   =&scheduler->TaskPoolArenas[pool->PoolIndex];
            taskenv->GlobalMemory  =&scheduler->GlobalMemoryArena;
            taskenv->IoThreadPool  = scheduler->IoThreadPool;
            taskenv->IoRequestPool =&scheduler->TaskIoRequestPools[pool->PoolIndex];
            return 0;
        }
        else
        {   // no task pools are available from the specified pool.
            OsLayerError("ERROR: %S(%u): Failed to allocate task pool from pool type %u. No task pools are available.\n", __FUNCTION__, GetCurrentThreadId(), pool_type);
            ZeroMemory(taskenv, sizeof(OS_TASK_ENVIRONMENT));
            return -1;
        }
    }
    else
    {   // the type identifier isn't valid, so fail immediately.
        OsLayerError("ERROR: %S(%u): Unable to find task pool type with ID %u.\n", __FUNCTION__, GetCurrentThreadId(), pool_type);
        ZeroMemory(taskenv, sizeof(OS_TASK_ENVIRONMENT));
        return -1;
    }
}

/// @summary Recycle a task pool, returning it for use by another thread.
/// @param taskenv The OS_TASK_ENVIRONMENT initialized by OsAllocateTaskPool.
public_function void
OsReturnTaskPool
(
    OS_TASK_ENVIRONMENT *taskenv
)
{   // sanity check - ensure the taskenv associated with the pool being returned is valid.
    if (taskenv->TaskPool == NULL)
    {
        OsLayerError("ERROR: %S(%u): Task pool double-free.\n", __FUNCTION__, GetCurrentThreadId());
        return;
    }
    // locate the PoolId in the list of pool types defined on the scheduler.
    uint32_t const *pool_ids = taskenv->TaskScheduler->PoolIdList;
    size_t   pool_type_index = 0;
    bool     pool_type_found = false;
    for (size_t i = 0, n = taskenv->TaskScheduler->PoolTypeCount; i < n; ++i)
    {
        if (pool_ids[i] == taskenv->TaskPool->PoolId)
        {
            pool_type_found = true;
            pool_type_index = i;
            break;
        }
    }
    if (pool_type_found)
    {   // return the pool to the free list.
        EnterCriticalSection(&taskenv->TaskScheduler->PoolFreeListLocks[pool_type_index]);
        {
            taskenv->TaskPool->NextFreePool = taskenv->TaskScheduler->PoolFreeLists[pool_type_index];
            taskenv->TaskScheduler->PoolFreeLists[pool_type_index] = taskenv->TaskPool;
        }
        LeaveCriticalSection(&taskenv->TaskScheduler->PoolFreeListLocks[pool_type_index]);
        // wipe out the task environment object to avoid double-frees.
        ZeroMemory(taskenv, sizeof(OS_TASK_ENVIRONMENT));
    }
    else
    {
        OsLayerError("ERROR: %S(%u): Unable to find task pool with ID %u.\n", __FUNCTION__, GetCurrentThreadId(), taskenv->TaskPool->PoolId);
        return;
    }
}

/// @summary Publish notifications to worker threads to steak tasks from the calling thread.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_count The number of steal notifications to publish.
public_function void
OsPublishTasks
(
    OS_TASK_ENVIRONMENT *taskenv, 
    size_t            task_count
)
{
    HANDLE         *iocp_list = taskenv->TaskScheduler->WorkerThreadPort;
    OS_TASK_POOL   *task_pool = taskenv->TaskPool;
    if ((task_pool->PoolUsage & OS_TASK_POOL_USAGE_FLAG_PUBLISH) == 0)
    {
        OsLayerError("ERROR: %S(%u): Attempt to publish %Iu tasks from thread without OS_TASK_POOL_USAGE_FLAG_PUBLISH.\n", __FUNCTION__, task_pool->ThreadId, task_count);
        return;
    }
    if (task_pool->WorkerCount == 0)
    {
        OsLayerError("ERROR: %S(%u): Attempt to publish %Iu tasks, but scheduler has no worker threads.\n", __FUNCTION__, task_pool->ThreadId, task_count);
        return;
    }
    for (size_t i = 0; i < task_count; ++i)
    {   // just go round-robin through the worker threads. allow NextWorker to wrap-around.
        uint16_t worker_index = (task_pool->NextWorker++) % task_pool->WorkerCount;
        if (PostQueuedCompletionStatus(iocp_list[worker_index], 1, (ULONG_PTR) task_pool, NULL) == FALSE)
        {
            OsLayerError("ERROR: %S(%u): Failed to publish steal notification to worker %u (%08X).\n", __FUNCTION__, task_pool->ThreadId, worker_index, GetLastError());
            return;
        }
    }
}

/// @summary Indicate the completion of a particular task. This function should be called from the thread that executed the task.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_id The identifier of the completed task.
/// @return The number of ready-to-run tasks added to the thread's local ready-to-run queue.
public_function size_t
OsCompleteTask
(
    OS_TASK_ENVIRONMENT *taskenv, 
    os_task_id_t         task_id
)
{   // multiple threads can be concurrently executing CompleteTask for the same task_id.
    // this can happen when multiple child tasks have finished executing on different 
    // threads, and are calling OsCompleteTask for their parent task.
    OS_TASK_POOL *task_pool = taskenv->TaskPool;
    OS_TASK_POOL *pool_list = taskenv->TaskPool->TaskPoolList;
    uint32_t const     tsrc =(task_id & OS_TASK_ID_MASK_POOL) >> OS_TASK_ID_SHIFT_POOL;
    uint32_t const     tidx =(task_id & OS_TASK_ID_MASK_INDEX) >> OS_TASK_ID_SHIFT_INDEX;
    OS_TASK_DATA      *task =&pool_list[tsrc].TaskPoolData[tidx];
    uint32_t          usage = task_pool->PoolUsage;
    size_t   ready_to_run_s = 0;
    size_t   ready_to_run_p = 0;
    os_task_id_t   *permits = NULL;
    int32_t        npermits = 0;
    int32_t      work_count = 0;

    // decrement the number of work items. when this counter reaches zero, the task is completed.
    if ((work_count  = task->WorkCount.fetch_sub(1, std::memory_order_seq_cst)) == 1)
    {   // the calling thread will process the permits list.
        permits      = task->PermitIds;
        npermits     = task->PermitCount.exchange(-1, std::memory_order_seq_cst);
        // process the permits list, decrementing the WaitCount for each permitted task.
        // if the WaitCount for a task reaches zero, the task is added to the ready-to-run queue.
        for (int32_t i = 0; i < npermits; ++i)
        {
            uint32_t const psrc = (permits[i] & OS_TASK_ID_MASK_POOL) >> OS_TASK_ID_SHIFT_POOL;
            uint32_t const pidx = (permits[i] & OS_TASK_ID_MASK_INDEX) >> OS_TASK_ID_SHIFT_INDEX;
            OS_TASK_DATA *ptask = &pool_list[psrc].TaskPoolData[pidx];
            if (ptask->WaitCount.fetch_add(1, std::memory_order_seq_cst) == -1)
            {   // this task is ready-to-run; push it onto the front of the local RTR queue.
                OsTaskQueuePush(&task_pool->WorkQueue, permits[i]);
                ready_to_run_s++;
            }
        }
        if (ready_to_run_s != 0)
        {   // if the pool doesn't have the EXECUTE usage flag specified, publish tasks.
            // if the pool does have the EXECUTE usage flag specified, the caller can decide 
            // if or when to make the tasks visible, and how many to make visible.
            if ((usage & OS_TASK_POOL_USAGE_FLAG_EXECUTE) == 0)
            {   // publish all of the available tasks.
                OsPublishTasks(taskenv, ready_to_run_s);
            }
        }

        // if the task has a parent, bubble the completion up the chain.
        if (task->ParentId != OS_INVALID_TASK_ID)
        {   // this may increase the number of ready-to-run tasks.
            if ((ready_to_run_p = OsCompleteTask(taskenv, task->ParentId)) > 0)
            {   // if the pool doesn't have the EXECUTE usage flag specified, publish tasks.
                if ((usage & OS_TASK_POOL_USAGE_FLAG_EXECUTE) == 0)
                {   // publish all of the available tasks.
                    OsPublishTasks(taskenv, ready_to_run_p);
                }
            }
        }

        // finally, mark the slot as being available on the owning task pool.
        pool_list[tsrc].SlotStatus[tidx].store(OS_TASK_SLOT_STATUS_FREE, std::memory_order_release);
    }
    return (ready_to_run_s + ready_to_run_p);
}

/// @summary Retrieve the OS_TASK_POOL_ERROR resulting from the most recent task definition.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the OS_TASK_POOL to query.
/// @return One of OS_TASK_POOL_ERROR.
public_function int
OsGetTaskPoolError
(
    OS_TASK_ENVIRONMENT *taskenv
)
{
    return taskenv->TaskPool->LastError;
}

/// @summary Set the OS_TASK_POOL_ERROR resulting from the most recent task definition attempt on a task pool.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the OS_TASK_POOL to update.
/// @param last_error One of OS_TASK_POOL_ERROR specifying the error code.
public_function void
OsSetTaskPoolLastError
(
    OS_TASK_ENVIRONMENT *taskenv, 
    int               last_error
)
{
    taskenv->TaskPool->LastError = last_error;
}

/// @summary Indicate that a task has been fully defined, and allow the task to complete.
/// @param taskenv The OS_TASK_ENVIRONMENT used to define the task.
/// @param task_id The identifier of the task being defined.
/// @return The number of ready-to-run tasks.
public_function size_t
OsFinishTaskDefinition
(
    OS_TASK_ENVIRONMENT *taskenv, 
    os_task_id_t         task_id
)
{
    if (task_id != OS_INVALID_TASK_ID)
    {
        return OsCompleteTask(taskenv, task_id);
    }
    else return 0;
}

/// @summary Execute tasks on the calling thread until the specified task has completed. The calling thread never enters an operating system wait state.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param wait_task The identifier of the task to wait for.
public_function void
OsWaitForTask
(
    OS_TASK_ENVIRONMENT *taskenv, 
    os_task_id_t       wait_task
)
{
    if ((wait_task & OS_TASK_ID_MASK_VALID) != 0)
    {   // the wait_task specifies a valid task. run tasks until it completes.
        uint32_t const    wsrc = (wait_task & OS_TASK_ID_MASK_POOL) >> OS_TASK_ID_SHIFT_POOL;
        uint32_t const    widx = (wait_task & OS_TASK_ID_MASK_INDEX) >> OS_TASK_ID_SHIFT_INDEX;
        size_t      pool_count =  taskenv->TaskScheduler->TaskPoolCount;
        OS_TASK_POOL     *self =  taskenv->TaskPool;
        OS_TASK_DATA     *wait = &self->TaskPoolList[wsrc].TaskPoolData[widx];
        OS_TASK_QUEUE   *local = &self->WorkQueue;
        size_t      this_index =  self->PoolIndex;
        size_t    victim_index =  0;
        os_task_id_t   work_id =  OS_INVALID_TASK_ID;
        bool         more_work =  false;
        while (wait->WorkCount.load(std::memory_order_seq_cst) != 0)
        {   // the task hasn't completed yet, so first try and take a task from the local ready-to-run queue.
            if ((work_id = OsTaskQueueTake(local, more_work)) == OS_INVALID_TASK_ID)
            {   
                do
                {   // continue to check for completion of the waited-on task.
                    if (wait->WorkCount.load(std::memory_order_seq_cst) == 0)
                        return;
                    // there's nothing in the local queue, so attempt to steal some work.
                    if ((victim_index = ((self->NextWorker++) % pool_count)) != this_index)
                    {   // attempt to steal a single task from the selected victim.
                        work_id = OsTaskQueueSteal(&self->TaskPoolList[victim_index].WorkQueue, more_work);
                    }
                } while(work_id == OS_INVALID_TASK_ID);
            }
            // at this point, work_id identifies a valid task, so execute it on this thread.
            // if this task spawns additional tasks, they'll appear in the local work queue.
            uint32_t const tsrc = (work_id & OS_TASK_ID_MASK_POOL) >> OS_TASK_ID_SHIFT_POOL;
            uint32_t const tidx = (work_id & OS_TASK_ID_MASK_INDEX) >> OS_TASK_ID_SHIFT_INDEX;
            OS_TASK_DATA  *task = &self->TaskPoolList[tsrc].TaskPoolData[tidx];
            OsMemoryArenaReset(taskenv->LocalMemory);
            task->TaskMain(work_id, task->TaskData, taskenv);
            OsCompleteTask(taskenv, work_id);
        }
    }
}

/// @summary Create a new task. If all dependencies have been satisfied, add the task to the ready-to-run queue. The task cannot complete until FinishTaskDefinition is called.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_type One of the values of the TASK_ID_TYPE enumeration specifying the type of task.
/// @param task_main The entry point of the new task.
/// @param task_args Optional data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @param args_size The size of the optional task data, in bytes.
/// @param dependency_list The optional list of task identifiers for all tasks that must complete before the new task is made ready-to-run.
/// @param dependency_count The number of valid task identifiers in the dependencies list.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function os_task_id_t
OsDefineTask
(
    OS_TASK_ENVIRONMENT        *taskenv, 
    uint32_t     const        task_type, 
    OS_TASK_ENTRYPOINT        task_main, 
    void         const       *task_args, 
    size_t       const        args_size, 
    os_task_id_t const *dependency_list,
    size_t       const dependency_count
)
{   // perform some optional runtime checks. these help to ensure correct usage.
    if (GetCurrentThreadId() != taskenv->ThreadId)
    {   // the calling thread must be the same thread that allocated the task pool.
        OsSetTaskPoolLastError(taskenv, OS_TASK_POOL_ERROR_INVALID_THREAD);
        assert(GetCurrentThreadId() == taskenv->ThreadId);
        return OS_INVALID_TASK_ID;
    }
    if (args_size > OS_TASK_DATA::MAX_DATA_BYTES)
    {   // the task-local parameter data is too large to fit inside the job structure.
        OsSetTaskPoolLastError(taskenv, OS_TASK_POOL_ERROR_DATA_LIMIT);
        assert(args_size <= OS_TASK_DATA::MAX_DATA_BYTES);
        return OS_INVALID_TASK_ID;
    }

    // reset the error code on the task pool.
    OsSetTaskPoolLastError(taskenv, OS_TASK_POOL_ERROR_NONE);

    // search for an available task slot in the task pool.
    OS_TASK_POOL::atomic_u8_t *slot_list = taskenv->TaskPool->SlotStatus;
    uint32_t const             slot_mask = taskenv->TaskPool->IndexMask;
    uint32_t                 array_index = taskenv->TaskPool->NextIndex;
    uint32_t                 start_index = taskenv->TaskPool->NextIndex;
    bool                      found_slot = false;
    bool                    ready_to_run = true;
    do
    {   // the calling thread is the only thread that can mark a slot as USED.
        // typically this will be a very short search; just one item.
        if (slot_list[array_index].load(std::memory_order_acquire) == OS_TASK_SLOT_STATUS_FREE)
        {   // the slot is currently unused; the search is finished.
            taskenv->TaskPool->NextIndex = ((array_index+1) & slot_mask);
            found_slot = true;
            break;
        }
    } while ((array_index = ((array_index+1) & slot_mask)) != start_index);

    if (!found_slot)
    {   // no task slots are available currently - try again later or increase the pool capacity.
        OsSetTaskPoolLastError(taskenv, OS_TASK_POOL_ERROR_TASK_LIMIT);
        return OS_INVALID_TASK_ID;
    }

    // initialize the task data slot. the WorkCount starts as 2; one for the task definition 
    // and one for the actual work executed by the task. this ensures that the task cannot 
    // complete (though it may execute) before this function returns. 
    os_task_id_t    task_id = OsMakeTaskId(task_type, taskenv->TaskPool->PoolIndex, array_index);
    OS_TASK_DATA *task_data = &taskenv->TaskPool->TaskPoolData[array_index];
    task_data->ParentId     = OS_INVALID_TASK_ID;
    task_data->TaskMain     = task_main;
    CopyMemory(task_data->TaskData, task_args, args_size);
    task_data->WorkCount.store(2, std::memory_order_release);
    task_data->PermitCount.store(0, std::memory_order_release);
    task_data->WaitCount.store(-int32_t(dependency_count), std::memory_order_relaxed);
    slot_list[array_index].store(OS_TASK_SLOT_STATUS_USED, std::memory_order_seq_cst);

    // convert dependencies into permits. this may make the task not ready-to-run.
    for (size_t i = 0; i < dependency_count; ++i)
    {
        uint32_t const  psrc = (dependency_list[i] & OS_TASK_ID_MASK_POOL) >> OS_TASK_ID_SHIFT_POOL;
        uint32_t const  pidx = (dependency_list[i] & OS_TASK_ID_MASK_INDEX) >> OS_TASK_ID_SHIFT_INDEX;
        OS_TASK_DATA *permit = &taskenv->TaskPool->TaskPoolList[psrc].TaskPoolData[pidx];
        int32_t            n =  permit->PermitCount.load(std::memory_order_relaxed);
        do
        {
            if (n < 0)
            {   // this dependency has already completed. the increment must be atomic 
                // because a previously created permit may be completing concurrently.
                // break out of the do...while loop to avoid updating the permit count.
                ready_to_run = task_data->WaitCount.fetch_add(1, std::memory_order_seq_cst) == -1;
                break;
            }
            if (n < OS_TASK_DATA::MAX_PERMITS)
            {   // append the task to the permits list of the permitting task.
                permit->PermitIds[n] = task_id;
                ready_to_run = false;
            }
            else
            {   // the task 'permit' allows too many tasks to execute. redesign your task structure.
                // in the future, maybe we can be better about this and use a linked-list instead.
                OsLayerError("ERROR: %S(%u): Exceeded permit limit on task %08X, dependency of task %08X.\n", __FUNCTION__, GetCurrentThreadId(), dependency_list[i], task_id);
                assert(n < OS_TASK_DATA::MAX_PERMITS);
                ready_to_run = task_data->WaitCount.fetch_add(1, std::memory_order_seq_cst) == -1;
                break;
            }
        } while (!permit->PermitCount.compare_exchange_weak(n, n+1, std::memory_order_seq_cst, std::memory_order_relaxed));
    }

    // if the task is ready-to-run, and is not an EXTERNAL task, add it to the local work queue.
    if (ready_to_run && task_type != OS_TASK_ID_TYPE_EXTERNAL)
    {   // push the task onto the private end of the thread-local queue.
        OsTaskQueuePush(&taskenv->TaskPool->WorkQueue, task_id);
        if ((taskenv->PoolUsage & OS_TASK_POOL_USAGE_FLAG_EXECUTE) == 0)
        {   // this task pool cannot execute tasks, so notify a worker thread to pick it up.
            OsPublishTasks(taskenv, 1);
        }
    }
    return task_id;
}

/// @summary Create a new child task. If all dependencies have been satisfied, add the task to the ready-to-run queue. The task cannot complete until FinishTaskDefinition is called.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_type One of the values of the TASK_ID_TYPE enumeration specifying the type of task.
/// @param task_main The entry point of the new task.
/// @param task_args Optional data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @param args_size The size of the optional task data, in bytes.
/// @param parent_id The valid identifier of the parent task, which must not have completed yet.
/// @param dependency_list The optional list of task identifiers for all tasks that must complete before the new task is made ready-to-run.
/// @param dependency_count The number of valid task identifiers in the dependencies list.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function os_task_id_t
OsDefineChildTask
(
    OS_TASK_ENVIRONMENT        *taskenv, 
    uint32_t     const        task_type, 
    OS_TASK_ENTRYPOINT        task_main, 
    void         const       *task_args, 
    size_t       const        args_size, 
    os_task_id_t const        parent_id,
    os_task_id_t const *dependency_list,
    size_t       const dependency_count
)
{   // perform some optional runtime checks. these help to ensure correct usage.
    if (GetCurrentThreadId() != taskenv->ThreadId)
    {   // the calling thread must be the same thread that allocated the task pool.
        OsSetTaskPoolLastError(taskenv, OS_TASK_POOL_ERROR_INVALID_THREAD);
        assert(GetCurrentThreadId() == taskenv->ThreadId);
        return OS_INVALID_TASK_ID;
    }
    if (args_size > OS_TASK_DATA::MAX_DATA_BYTES)
    {   // the task-local parameter data is too large to fit inside the job structure.
        OsSetTaskPoolLastError(taskenv, OS_TASK_POOL_ERROR_DATA_LIMIT);
        assert(args_size <= OS_TASK_DATA::MAX_DATA_BYTES);
        return OS_INVALID_TASK_ID;
    }
    if ((parent_id & OS_TASK_ID_MASK_VALID) == 0)
    {   // the parent task is invalid; use OsDefineTask instead.
        OsSetTaskPoolLastError(taskenv, OS_TASK_POOL_ERROR_INVALID_PARENT);
        assert((parent_id & OS_TASK_ID_MASK_VALID) != 0);
        return OS_INVALID_TASK_ID;
    }

    // reset the error code on the task pool.
    OsSetTaskPoolLastError(taskenv, OS_TASK_POOL_ERROR_NONE);

    // search for an available task slot in the task pool.
    OS_TASK_POOL::atomic_u8_t *slot_list = taskenv->TaskPool->SlotStatus;
    uint32_t const             slot_mask = taskenv->TaskPool->IndexMask;
    uint32_t                 array_index = taskenv->TaskPool->NextIndex;
    uint32_t                 start_index = taskenv->TaskPool->NextIndex;
    bool                      found_slot = false;
    bool                    ready_to_run = true;
    do
    {   // the calling thread is the only thread that can mark a slot as USED.
        // typically this will be a very short search; just one item.
        if (slot_list[array_index].load(std::memory_order_acquire) == OS_TASK_SLOT_STATUS_FREE)
        {   // the slot is currently unused; the search is finished.
            taskenv->TaskPool->NextIndex = ((array_index+1) & slot_mask);
            found_slot = true;
            break;
        }
    } while ((array_index = ((array_index+1) & slot_mask)) != start_index);

    if (!found_slot)
    {   // no task slots are available currently - try again later or increase the pool capacity.
        OsSetTaskPoolLastError(taskenv, OS_TASK_POOL_ERROR_TASK_LIMIT);
        return OS_INVALID_TASK_ID;
    }

    // add an outstanding work item on the parent task to represent the child task.
    uint32_t const  fsrc = (parent_id & OS_TASK_ID_MASK_POOL) >> OS_TASK_ID_SHIFT_POOL;
    uint32_t const  fidx = (parent_id & OS_TASK_ID_MASK_INDEX) >> OS_TASK_ID_SHIFT_INDEX;
    OS_TASK_DATA *parent = &taskenv->TaskPool->TaskPoolList[fsrc].TaskPoolData[fidx];
    parent->WorkCount.fetch_add(1, std::memory_order_seq_cst);

    // initialize the task data slot. the WorkCount starts as 2; one for the task definition 
    // and one for the actual work executed by the task. this ensures that the task cannot 
    // complete (though it may execute) before this function returns. 
    os_task_id_t    task_id = OsMakeTaskId(task_type, taskenv->TaskPool->PoolIndex, array_index);
    OS_TASK_DATA *task_data = &taskenv->TaskPool->TaskPoolData[array_index];
    task_data->ParentId     = parent_id;
    task_data->TaskMain     = task_main;
    CopyMemory(task_data->TaskData, task_args, args_size);
    task_data->WorkCount.store(2, std::memory_order_release);
    task_data->PermitCount.store(0, std::memory_order_release);
    task_data->WaitCount.store(-int32_t(dependency_count), std::memory_order_relaxed);
    slot_list[array_index].store(OS_TASK_SLOT_STATUS_USED, std::memory_order_seq_cst);

    // convert dependencies into permits. this may make the task not ready-to-run.
    for (size_t i = 0; i < dependency_count; ++i)
    {
        uint32_t const  psrc = (dependency_list[i] & OS_TASK_ID_MASK_POOL) >> OS_TASK_ID_SHIFT_POOL;
        uint32_t const  pidx = (dependency_list[i] & OS_TASK_ID_MASK_INDEX) >> OS_TASK_ID_SHIFT_INDEX;
        OS_TASK_DATA *permit = &taskenv->TaskPool->TaskPoolList[psrc].TaskPoolData[pidx];
        int32_t            n =  permit->PermitCount.load(std::memory_order_relaxed);
        do
        {
            if (n < 0)
            {   // this dependency has already completed. the increment must be atomic 
                // because a previously created permit may be completing concurrently.
                // break out of the do...while loop to avoid updating the permit count.
                ready_to_run = task_data->WaitCount.fetch_add(1, std::memory_order_seq_cst) == -1;
                break;
            }
            if (n < OS_TASK_DATA::MAX_PERMITS)
            {   // append the task to the permits list of the permitting task.
                permit->PermitIds[n] = task_id;
                ready_to_run = false;
            }
            else
            {   // the task 'permit' allows too many tasks to execute. redesign your task structure.
                // in the future, maybe we can be better about this and use a linked-list instead.
                OsLayerError("ERROR: %S(%u): Exceeded permit limit on task %08X, dependency of task %08X.\n", __FUNCTION__, GetCurrentThreadId(), dependency_list[i], task_id);
                assert(n < OS_TASK_DATA::MAX_PERMITS);
                ready_to_run = task_data->WaitCount.fetch_add(1, std::memory_order_seq_cst) == -1;
                break;
            }
        } while (!permit->PermitCount.compare_exchange_weak(n, n+1, std::memory_order_seq_cst, std::memory_order_relaxed));
    }

    // if the task is ready-to-run, and is not an EXTERNAL task, add it to the local work queue.
    if (ready_to_run && task_type != OS_TASK_ID_TYPE_EXTERNAL)
    {   // push the task onto the private end of the thread-local queue.
        OsTaskQueuePush(&taskenv->TaskPool->WorkQueue, task_id);
        if ((taskenv->PoolUsage & OS_TASK_POOL_USAGE_FLAG_EXECUTE) == 0)
        {   // this task pool cannot execute tasks, so notify a worker thread to pick it up.
            OsPublishTasks(taskenv, 1);
        }
    }
    return task_id;
}

/// @summary Create a new task and call OsFinishTaskDefinition. If all dependencies have been satisfied, add the task to the ready-to-run queue.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_type One of the values of the OS_TASK_ID_TYPE enumeration specifying the type of task.
/// @param task_main The entry point of the new task.
/// @param task_args Optional data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @param args_size The size of the optional task data, in bytes.
/// @param dependency_list The optional list of task identifiers for all tasks that must complete before the new task is made ready-to-run.
/// @param dependency_count The number of valid task identifiers in the dependency list.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function inline os_task_id_t
OsSpawnTask
(
    OS_TASK_ENVIRONMENT         *taskenv, 
    uint32_t     const         task_type, 
    OS_TASK_ENTRYPOINT         task_main, 
    void         const        *task_args, 
    size_t       const         args_size, 
    os_task_id_t const  *dependency_list, 
    size_t       const  dependency_count
)
{
    os_task_id_t task_id = OsDefineTask(taskenv, task_type, task_main, task_args, args_size, dependency_list, dependency_count);
    OsFinishTaskDefinition(taskenv, task_id);
    return task_id;
}

/// @summary Create a new task and call OsFinishTaskDefinition. If all dependencies have been satisfied, add the task to the ready-to-run queue.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_type One of the values of the OS_TASK_ID_TYPE enumeration specifying the type of task.
/// @param task_main The entry point of the new task.
/// @param task_args Optional data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @param args_size The size of the optional task data, in bytes.
/// @param parent_id The valid identifier of the parent task, which must not have completed yet.
/// @param dependency_list The optional list of task identifiers for all tasks that must complete before the new task is made ready-to-run.
/// @param dependency_count The number of valid task identifiers in the dependency list.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function inline os_task_id_t
OsSpawnChildTask
(
    OS_TASK_ENVIRONMENT         *taskenv, 
    uint32_t     const         task_type, 
    OS_TASK_ENTRYPOINT         task_main, 
    void         const        *task_args, 
    size_t       const         args_size, 
    os_task_id_t const         parent_id,
    os_task_id_t const  *dependency_list, 
    size_t       const  dependency_count
)
{
    os_task_id_t task_id = OsDefineChildTask(taskenv, task_type, task_main, task_args, args_size, parent_id, dependency_list, dependency_count);
    OsFinishTaskDefinition(taskenv, task_id);
    return task_id;
}

/// @summary Create a new task that is completed based on an external event. Do not call OsFinishTaskDefinition. Call OsCompleteTask when the external event occurs.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function inline os_task_id_t
OsCreateExternalTask
(
    OS_TASK_ENVIRONMENT *taskenv 
)
{
    os_task_id_t task_id = OsDefineTask(taskenv, OS_TASK_ID_TYPE_EXTERNAL, NULL, NULL, 0, NULL, 0);
    OsFinishTaskDefinition(taskenv, task_id); // decrement the outstanding work counter to 1
    return task_id;
}

/// @summary Create a new child task that is completed based on an external event. Do not call OsFinishTaskDefinition. Call OsCompleteTask when the external event occurs.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param parent_id The valid identifier of the parent task. The parent task will not complete until all children have completed.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function os_task_id_t
OsCreateExternalChildTask
(
    OS_TASK_ENVIRONMENT *taskenv,
    os_task_id_t const parent_id
)
{
    os_task_id_t task_id = OsDefineChildTask(taskenv, OS_TASK_ID_TYPE_EXTERNAL, NULL, NULL, 0, parent_id, NULL, 0);
    OsFinishTaskDefinition(taskenv, task_id); // decrement the outstanding work counter to 1
    return task_id;
}

/// @summary Create a new task and add the task to the ready-to-run queue. The task cannot complete before OsFinishTaskDefinition is called.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function inline os_task_id_t
OsDefineTask
(
    OS_TASK_ENVIRONMENT *taskenv,
    OS_TASK_ENTRYPOINT task_main
)
{
    return OsDefineTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, NULL, 0, NULL, 0);
}

/// @summary Create a new task and call OsFinishTaskDefinition. Add the task to the ready-to-run queue.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function inline os_task_id_t
OsSpawnTask
(
    OS_TASK_ENVIRONMENT *taskenv,
    OS_TASK_ENTRYPOINT task_main
)
{
    os_task_id_t task_id = OsDefineTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, NULL, 0, NULL, 0);
    OsFinishTaskDefinition(taskenv, task_id);
    return task_id;
}

/// @summary Create a new task and add the task to the ready-to-run queue. The task cannot complete before FinishTaskDefinition is called.
/// @typeparam ArgsType The type of the task argument data.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param task_args Data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @return The identifier of the new task, or INVALID_TASK_ID.
template <typename ArgsType>
public_function inline os_task_id_t
OsDefineTask
(
    OS_TASK_ENVIRONMENT *taskenv,
    OS_TASK_ENTRYPOINT task_main,
    ArgsType const    *task_args 
)
{
    return OsDefineTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, task_args, sizeof(ArgsType), NULL, 0);
}

/// @summary Create a new task and call OsFinishTaskDefinition. Add the task to the ready-to-run queue.
/// @typeparam ArgsType The type of the task argument data.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param task_args Data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
template <typename ArgsType>
public_function inline os_task_id_t
OsSpawnTask
(
    OS_TASK_ENVIRONMENT *taskenv,
    OS_TASK_ENTRYPOINT task_main,
    ArgsType const    *task_args 
)
{
    os_task_id_t task_id = OsDefineTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, task_args, sizeof(ArgsType), NULL, 0);
    OsFinishTaskDefinition(taskenv, task_id);
    return task_id;
}

/// @summary Create a new task and call OsFinishTaskDefinition. If all dependencies have been satisfied, add the task to the ready-to-run queue.
/// @typeparam ArgsType The type of the task argument data.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param task_args Data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @param dependency_list The list of task identifiers for all tasks that must complete before the new task is made ready-to-run.
/// @param dependency_count The number of valid task identifiers in the dependencies list.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
template <typename ArgsType>
public_function inline os_task_id_t
OsSpawnTask
(
    OS_TASK_ENVIRONMENT         *taskenv,
    OS_TASK_ENTRYPOINT         task_main,
    ArgsType     const        *task_args, 
    os_task_id_t const  *dependency_list, 
    size_t       const  dependency_count
)
{
    os_task_id_t task_id = OsDefineTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, task_args, sizeof(ArgsType), dependency_list, dependency_count);
    OsFinishTaskDefinition(taskenv, task_id);
    return task_id;
}

/// @summary Create a new child task and add the task to the ready-to-run queue. The task cannot complete until OsFinishTaskDefinition is called.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param parent_id The identifier of the parent task. This must specify a valid task ID.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function inline os_task_id_t
OsDefineChildTask
(
    OS_TASK_ENVIRONMENT  *taskenv,
    OS_TASK_ENTRYPOINT  task_main,
    os_task_id_t const  parent_id
)
{
    return OsDefineChildTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, NULL, 0, parent_id, NULL, 0);
}

/// @summary Create a new child task and call OsFinishTaskDefinition. Add the task to the ready-to-run queue.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param parent_id The identifier of the parent task. This must specify a valid task ID.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function inline os_task_id_t
OsSpawnChildTask
(
    OS_TASK_ENVIRONMENT  *taskenv,
    OS_TASK_ENTRYPOINT  task_main,
    os_task_id_t const  parent_id
)
{
    os_task_id_t task_id = OsDefineChildTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, NULL, 0, parent_id, NULL, 0);
    OsFinishTaskDefinition(taskenv, task_id);
    return task_id;
}

/// @summary Create a new child task. If all dependencies have been satisfied, add the task to the ready-to-run queue. The task cannot complete until OsFinishTaskDefinition is called.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param parent_id The identifier of the parent task. This must specify a valid task ID.
/// @param dependency_list The list of task identifiers for all tasks that must complete before the new task is made ready-to-run.
/// @param dependency_count The number of valid task identifiers in the dependency list.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function inline os_task_id_t
OsDefineChildTask
(
    OS_TASK_ENVIRONMENT        *taskenv,
    OS_TASK_ENTRYPOINT        task_main,
    os_task_id_t const        parent_id,
    os_task_id_t const *dependency_list, 
    size_t       const dependency_count
)
{
    return OsDefineChildTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, NULL, 0, parent_id, dependency_list, dependency_count);
}

/// @summary Create a new task and call OsFinishTaskDefinition. If all dependencies have been satisfied, add the task to the ready-to-run queue.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param parent_id The identifier of the parent task.
/// @param dependency_list The list of task identifiers for all tasks that must complete before the new task is made ready-to-run.
/// @param dependency_count The number of valid task identifiers in the dependency list.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
public_function inline os_task_id_t
OsSpawnChildTask
(
    OS_TASK_ENVIRONMENT        *taskenv,
    OS_TASK_ENTRYPOINT        task_main,
    os_task_id_t const        parent_id,
    os_task_id_t const *dependency_list, 
    size_t       const dependency_count
)
{
    os_task_id_t task_id = OsDefineChildTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, NULL, 0, parent_id, dependency_list, dependency_count);
    OsFinishTaskDefinition(taskenv, task_id);
    return task_id;
}

/// @summary Create a new child task and add the task to the ready-to-run queue.
/// @typeparam ArgsType The type of the task argument data.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param task_args Data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @param parent_id The identifier of the parent task.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
template <typename ArgsType>
public_function inline os_task_id_t
OsDefineChildTask
(
    OS_TASK_ENVIRONMENT  *taskenv,
    OS_TASK_ENTRYPOINT  task_main,
    ArgsType     const *task_args, 
    os_task_id_t const  parent_id
)
{
    return OsDefineChildTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, task_args, sizeof(ArgsType), parent_id, NULL, 0);
}

/// @summary Create a new child task and call FinishTaskDefinition. Add the task to the ready-to-run queue.
/// @typeparam ArgsType The type of the task argument data.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param task_args Data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @param parent_id The identifier of the parent task.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
template <typename ArgsType>
public_function inline os_task_id_t
OsSpawnChildTask
(
    OS_TASK_ENVIRONMENT  *taskenv,
    OS_TASK_ENTRYPOINT  task_main,
    ArgsType     const *task_args, 
    os_task_id_t const  parent_id
)
{
    os_task_id_t task_id = OsDefineChildTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, task_args, sizeof(ArgsType), parent_id, NULL, 0);
    OsFinishTaskDefinition(taskenv, task_id);
    return task_id;
}

/// @summary Create a new child task. If all dependencies have been satisfied, add the task to the ready-to-run queue. The task cannot complete until OsFinishTaskDefinition is called.
/// @typeparam ArgsType The type of the task argument data.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param task_args Data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @param parent_id The identifier of the parent task.
/// @param dependency_list The list of task identifiers for all tasks that must complete before the new task is made ready-to-run.
/// @param dependency_count The number of valid task identifiers in the dependencies list.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
template <typename ArgsType>
public_function inline os_task_id_t
OsDefineChildTask
(
    OS_TASK_ENVIRONMENT        *taskenv,
    OS_TASK_ENTRYPOINT        task_main,
    ArgsType     const       *task_args, 
    os_task_id_t const        parent_id,
    os_task_id_t const *dependency_list, 
    size_t       const dependency_count
)
{
    return OsDefineChildTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, task_args, sizeof(ArgsType), parent_id, dependency_list, dependency_count);
}

/// @summary Create a new child task and call OsFinishTaskDefinition. If all dependencies have been satisfied, add the task to the ready-to-run queue.
/// @typeparam ArgsType The type of the task argument data.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param task_main The entry point of the new task.
/// @param task_args Data to be supplied to the task when it executes. This data is memcpy'd into the new task.
/// @param parent_id The identifier of the parent task.
/// @param dependency_list The list of task identifiers for all tasks that must complete before the new task is made ready-to-run.
/// @param dependency_count The number of valid task identifiers in the dependencies list.
/// @return The identifier of the new task, or OS_INVALID_TASK_ID.
template <typename ArgsType>
public_function inline os_task_id_t
OsSpawnChildTask
(
    OS_TASK_ENVIRONMENT        *taskenv,
    OS_TASK_ENTRYPOINT        task_main,
    ArgsType     const       *task_args, 
    os_task_id_t const        parent_id,
    os_task_id_t const *dependency_list, 
    size_t       const dependency_count
)
{
    os_task_id_t task_id = OsDefineChildTask(taskenv, OS_TASK_ID_TYPE_INTERNAL, task_main, task_args, sizeof(ArgsType), parent_id, dependency_list, dependency_count);
    OsFinishTaskDefinition(taskenv, task_id);
    return task_id;
}

/// @summary Allocate the operating system object necessary to wait for task completion. The object is placed into a non-signaled state.
/// @param fence The OS_TASK_FENCE to allocate.
/// @return Zero if the fence object is successfully allocated, or non-zero if an error occurs.
public_function int
OsAllocateTaskFence
(
    OS_TASK_FENCE *fence
)
{   // initialize the fields of the OS_TASK_FENCE instance.
    ZeroMemory(fence, sizeof(OS_TASK_FENCE));

    // allocate a new manual-reset event in the non-signaled state.
    if ((fence->FenceSignal = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Failed to allocate manual-reset event for task fence (%08X).\n", __FUNCTION__, GetCurrentThreadId(), GetLastError());
        return -1;
    }
    return 0;
}

/// @summary Delete an OS_TASK_FENCE object.
/// @param fence The OS_TASK_FENCE to delete.
public_function void
OsDestroyTaskFence
(
    OS_TASK_FENCE *fence
)
{
    if (fence->FenceSignal != NULL)
    {
        CloseHandle(fence->FenceSignal);
        fence->FenceSignal = NULL;
    }
}

/// @summary Place a task fence into a non-signaled state.
/// @param fence The OS_TASK_FENCE to reset.
public_function void
OsResetTaskFence
(
    OS_TASK_FENCE *fence
)
{
    ResetEvent(fence->FenceSignal);
}

/// @summary Block the calling thread until a task fence enters the signaled state (all of its dependent tasks have completed.)
/// @param fence The OS_TASK_FENCE to wait on.
/// @param timeout_ns The maximum amount of time to wait, specified in nanoseconds.
/// @return true if the task fence becomes signaled, or false if a timeout or error occurs.
public_function bool
OsWaitTaskFence
(
    OS_TASK_FENCE *fence, 
    uint64_t  timeout_ns=0xFFFFFFFFFFFFFFFFULL
)
{
    uint32_t timeout_ms = (timeout_ns == 0xFFFFFFFFFFFFFFFFULL) ? INFINITE : OsNanosecondsToWholeMilliseconds(timeout_ns);
    return (WaitForSingleObject(fence->FenceSignal, timeout_ms) == WAIT_OBJECT_0);
}

/// @summary Implement the entry point for a fence task. The state of the associated fence is set to signaled.
/// @param task_id The identifier of the fence task.
/// @param task_args Parameter data associated with the fence task. In this case, this is a pointer to the OS_TASK_FENCE to signal.
/// @param taskenv The OS_TASK_ENVIRONMENT for the thread executing the task.
public_function void
OsFenceTaskMain
(
    os_task_id_t         task_id, 
    void              *task_args, 
    OS_TASK_ENVIRONMENT *taskenv
)
{
    OS_PROFILE_TASK(task_id, taskenv);
    {
        UNREFERENCED_PARAMETER(task_id);
        UNREFERENCED_PARAMETER(taskenv);
        OS_TASK_FENCE *fence =(OS_TASK_FENCE*) task_args;
        SetEvent(fence->FenceSignal);
    }
}

/// @summary Create a task that waits on one or more previously-defined tasks to complete. An operating system thread can enter a wait state until the fence becomes signaled. The fence is reset to a non-signaled state before the new task is created.
/// @param taskenv The OS_TASK_ENVIRONMENT associated with the calling thread.
/// @param fence The OS_TASK_FENCE object to reset and signal when all tasks in dependency_list have completed.
/// @param dependency_list The list of task IDs that must complete before the task fence becomes signaled.
/// @param dependency_count The number of task IDs in the dependency list.
/// @return The identifier of the fence task, or OS_INVALID_TASK_ID.
public_function os_task_id_t
OsCreateTaskFence
(
    OS_TASK_ENVIRONMENT        *taskenv, 
    OS_TASK_FENCE                *fence, 
    os_task_id_t const *dependency_list, 
    size_t       const dependency_count
)
{
    if (dependency_count < 1)
    {
        OsLayerError("ERROR: %S(%u): A task fence needs to have a non-empty dependency list.\n", __FUNCTION__, GetCurrentThreadId());
        return OS_INVALID_TASK_ID;
    }
    if (fence->FenceSignal == NULL)
    {   // allocate the fence object for the caller.
        if (OsAllocateTaskFence(fence) < 0)
        {
            OsLayerError("ERROR: %S(%u): Failed to allocate task fence.\n", __FUNCTION__, GetCurrentThreadId());
            return OS_INVALID_TASK_ID;
        }
    }
    // ensure that the manual-reset event is non-signaled.
    ResetEvent(fence->FenceSignal);
    // spawn the task; fence tasks do not have any children.
    return OsSpawnTask(taskenv, OsFenceTaskMain, fence, dependency_list, dependency_count);
}

/// @summary Implement the internal entry point of a worker thread.
/// @param argp Pointer to an OS_WORKER_THREAD_INIT instance specific to this thread.
/// @return Zero if the thread terminated normally, or non-zero for abnormal termination.
public_function unsigned int __cdecl
OsWorkerThreadMain
(
    void *argp
)
{
    OS_WORKER_THREAD_INIT  init = {};
    OS_WORKER_THREAD       args = {};
    OS_MEMORY_ARENA       arena = {};
    os_arena_marker_t user_mark = 0;
    uintptr_t        signal_arg = 0;
    const DWORD          LAUNCH = 0;
    const DWORD       TERMINATE = 1;
    const DWORD      WAIT_COUNT = 2;
    HANDLE   waitset[WAIT_COUNT]= {};
    DWORD                   tid = GetCurrentThreadId();
    DWORD                waitrc = 0;
    bool           keep_running = true;
    int             wake_reason = OS_WORKER_THREAD_WAKE_FOR_EXIT;
    unsigned int      exit_code = 1;

    // copy the initialization data into local stack memory.
    // argp may have been allocated on the stack of the caller 
    // and is only guaranteed to remain valid until the ReadySignal is set.
    CopyMemory(&init, argp, sizeof(OS_WORKER_THREAD_INIT));

    // spit out a message just prior to initialization:
    OsLayerOutput("START: %S(%u): Worker thread starting on pool 0x%p.\n", __FUNCTION__, tid, init.ThreadPool);

    // create a thread-local memory arena to be used by the callback executing on the worker.
    if (OsCreateMemoryArena(&arena, init.ArenaSize, false, true) < 0)
    {   // OsCreateMemoryArena outputs its own error information.
        OsLayerError("DEATH: %S(%u): Worker terminating in pool 0x%p.\n", __FUNCTION__, tid, init.ThreadPool);
        SetEvent(init.ErrorSignal);
        return 1;
    }

    // set up the data to be passed through to the application executing on the worker.
    args.ThreadPool     = init.ThreadPool;
    args.ThreadArena    = &arena;
    args.CompletionPort = init.CompletionPort;
    args.PoolContext    = init.PoolContext;
    args.ThreadContext  = NULL;
    args.ArenaSize      = OsMemoryArenaBytesReserved(&arena);
    args.ThreadId       = tid;

    // allow the application to perform per-thread setup.
    if (init.ThreadInit(&args) < 0)
    {
        OsLayerError("ERROR: %S(%u): Application thread initialization failed on pool 0x%p.\n", __FUNCTION__, tid, init.ThreadPool);
        OsLayerError("DEATH: %S(%u): Worker terminating in pool 0x%p.\n", __FUNCTION__, tid, init.ThreadPool);
        OsDeleteMemoryArena(&arena);
        SetEvent(init.ErrorSignal);
        return 2;
    }

    // save the thread-local memory arena marker.
    // this allows global per-thread data to be maintained, but task data to be reset.
    user_mark = OsMemoryArenaMark(&arena);

    // signal the main thread that this thread is ready to run.
    SetEvent(init.ReadySignal);

    // enter a wait state until the thread pool launches all of the workers.
    waitset[LAUNCH]     = init.LaunchSignal;
    waitset[TERMINATE]  = init.TerminateSignal;
    switch ((waitrc = WaitForMultipleObjects(WAIT_COUNT, waitset, FALSE, INFINITE)))
    {
        case WAIT_OBJECT_0 + LAUNCH:
            {   // enter the main wait loop.
                keep_running = true;
            } break;
        case WAIT_OBJECT_0 + TERMINATE:
            {   // do not enter the main wait loop.
                keep_running = false;
                exit_code = 0;
            } break; 
        default: 
            {   // do not enter the main wait loop.
                OsLayerError("ERROR: %S(%u): Unexpected result 0x%08X while waiting for launch signal.\n", __FUNCTION__, tid, waitrc);
                keep_running = false;
                exit_code = 1;
            } break;
    }

    __try
    {
        while (keep_running)
        {   // once launched by the thread pool, enter the main wait loop.
            switch ((wake_reason = OsWorkerThreadWaitForWakeup(init.CompletionPort, init.TerminateSignal, tid, signal_arg)))
            {
                case OS_WORKER_THREAD_WAKE_FOR_EXIT:
                    {   // allow the application to clean up any thread-local resources.
                        init.ThreadMain(&args, signal_arg, wake_reason);
                        keep_running = false;
                        exit_code = 0;
                    } break;
                case OS_WORKER_THREAD_WAKE_FOR_SIGNAL:
                case OS_WORKER_THREAD_WAKE_FOR_RUN:
                    {   // allow the application to execute the signal handler or work item.
                        init.ThreadMain(&args, signal_arg, wake_reason);
                    } break;
                case OS_WORKER_THREAD_WAKE_FOR_ERROR:
                    {   // OsWorkerThreadWaitForWakeup output error information already.
                        OsLayerError("ERROR: %S(%u): Worker terminating due to previous error(s).\n", __FUNCTION__, tid);
                        keep_running = false;
                        exit_code = 1;
                    } break;
                default:
                    {   // spit out an error if we get an unexpected return value.
                        assert(false && __FUNCTION__ ": Unexpected wake_reason.");
                        OsLayerError("ERROR: %S(%u): Worker terminating due to unexpected wake reason.\n", __FUNCTION__, tid);
                        keep_running = false;
                        exit_code = 1;
                    } break;
            }
            // reset the thread-local memory arena for the next iteration.
            OsMemoryArenaResetToMarker(&arena, user_mark);
            // reset the wake signal to 0/NULL for the next iteration.
            signal_arg = 0;
        }
    }
    __finally
    {   // the worker is terminating - clean up thread-local resources.
        // the ThreadMain will not be called again by this thread.
        OsDeleteMemoryArena(&arena);
        // spit out a message just prior to termination.
        OsLayerOutput("DEATH: %S(%u): Worker terminating in pool 0x%p.\n", __FUNCTION__, tid, init.ThreadPool);
        return exit_code;
    }
}

/// @summary Calculate the amount of memory required to create an OS thread pool.
/// @param thread_count The number of threads in the thread pool.
/// @return The number of bytes required to create an OS_THREAD_POOL with the specified number of worker threads. This value does not include the thread-local memory or thread stack memory.
public_function size_t
OsAllocationSizeForThreadPool
(
    size_t thread_count
)
{
    size_t size_in_bytes = 0;
    size_in_bytes += OsAllocationSizeForArray<unsigned int>(thread_count);
    size_in_bytes += OsAllocationSizeForArray<HANDLE>(thread_count);
    size_in_bytes += OsAllocationSizeForArray<HANDLE>(thread_count);
    size_in_bytes += OsAllocationSizeForArray<HANDLE>(thread_count);
    return size_in_bytes;
}

/// @summary Create a thread pool. The calling thread is blocked until all worker threads have successfully started and initialized.
/// @param pool The OS_THREAD_POOL instance to initialize.
/// @param init An OS_THREAD_POOL_INIT object describing the thread pool configuration.
/// @param arena The memory arena from which thread pool storage will be allocated.
/// @param name A zero-terminated string constant specifying a human-readable name for the thread pool, or NULL. This name is used for task profiler display.
/// @return Zero if the thread pool is created successfully and worker threads are ready-to-run, or -1 if an error occurs.
public_function int
OsCreateThreadPool
(
    OS_THREAD_POOL      *pool, 
    OS_THREAD_POOL_INIT *init,
    OS_MEMORY_ARENA    *arena, 
    char const          *name=NULL
)
{
    HANDLE                  iocp = NULL;
    HANDLE            evt_launch = NULL;
    HANDLE         evt_terminate = NULL;
    DWORD                    tid = GetCurrentThreadId();
    os_arena_marker_t mem_marker = OsMemoryArenaMark(arena);
    size_t        bytes_required = OsAllocationSizeForThreadPool(init->ThreadCount);
    size_t        align_required = std::alignment_of<HANDLE>::value;
    CV_PROVIDER     *cv_provider = NULL;
    CV_MARKERSERIES   *cv_series = NULL;
    HRESULT            cv_result = S_OK;
    char             cv_name[64] = {};

    if (!OsMemoryArenaCanSatisfyAllocation(arena, bytes_required, align_required))
    {
        OsLayerError("ERROR: %S(%u): Insufficient memory to create thread pool.\n", __FUNCTION__, tid);
        ZeroMemory(pool, sizeof(OS_THREAD_POOL));
        return -1;
    }
    if ((evt_launch = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // without the launch event, there's no way to synchronize worker launch.
        OsLayerError("ERROR: %S(%u): Unable to create pool launch event (0x%08X).\n", __FUNCTION__, tid, GetLastError());
        ZeroMemory(pool, sizeof(OS_THREAD_POOL));
        return -1;
    }
    if ((evt_terminate = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // without the termination event, there's no way to synchronize worker shutdown.
        OsLayerError("ERROR: %S(%u): Unable to create pool termination event (0x%08X).\n", __FUNCTION__, tid, GetLastError());
        ZeroMemory(pool, sizeof(OS_THREAD_POOL));
        CloseHandle(evt_launch);
        return -1;
    }
    if ((iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, (DWORD) init->ThreadCount+1)) == NULL)
    {   // without the completion port, there's no way to synchronize worker execution.
        OsLayerError("ERROR: %S(%u): Unable to create pool I/O completion port (0x%08X).\n", __FUNCTION__, tid, GetLastError());
        ZeroMemory(pool, sizeof(OS_THREAD_POOL));
        CloseHandle(evt_terminate);
        CloseHandle(evt_launch);
        return -1;
    }

    if (name == NULL)
    {   // Concurrency Visualizer SDK requires a non-NULL string.
        sprintf_s(cv_name, "Unnamed pool 0x%p", pool);
    }
    if (!SUCCEEDED((cv_result = CvInitProvider(&TaskProfilerGUID, &cv_provider))))
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize task profiler provider (HRESULT 0x%08X).\n", __FUNCTION__, tid, cv_result);
        ZeroMemory(pool, sizeof(OS_THREAD_POOL));
        CloseHandle(evt_terminate);
        CloseHandle(evt_launch);
        CloseHandle(iocp);
        return -1;
    }
    if (!SUCCEEDED((cv_result = CvCreateMarkerSeriesA(cv_provider, name, &cv_series))))
    {
        OsLayerError("ERROR: %S(%u): Unable to create task profiler marker series (HRESULT 0x%08X).\n", __FUNCTION__, tid, cv_result);
        ZeroMemory(pool, sizeof(OS_THREAD_POOL));
        CvReleaseProvider(cv_provider);
        CloseHandle(evt_terminate);
        CloseHandle(evt_launch);
        CloseHandle(iocp);
        return -1;
    }

    // initialize the thread pool fields and allocate memory for per-thread arrays.
    pool->ActiveThreads   = 0;
    pool->OSThreadIds     = OsMemoryArenaAllocateArray<unsigned int>(arena, init->ThreadCount);
    pool->OSThreadHandle  = OsMemoryArenaAllocateArray<HANDLE      >(arena, init->ThreadCount);
    pool->WorkerReady     = OsMemoryArenaAllocateArray<HANDLE      >(arena, init->ThreadCount);
    pool->WorkerError     = OsMemoryArenaAllocateArray<HANDLE      >(arena, init->ThreadCount);
    pool->CompletionPort  = iocp;
    pool->LaunchSignal    = evt_launch;
    pool->TerminateSignal = evt_terminate;
    pool->TaskProfiler.Provider     = cv_provider;
    pool->TaskProfiler.MarkerSeries = cv_series;
    ZeroMemory(pool->OSThreadIds    , init->ThreadCount * sizeof(unsigned int));
    ZeroMemory(pool->OSThreadHandle , init->ThreadCount * sizeof(HANDLE));
    ZeroMemory(pool->WorkerReady    , init->ThreadCount * sizeof(HANDLE));
    ZeroMemory(pool->WorkerError    , init->ThreadCount * sizeof(HANDLE));

    // set up the worker init structure and spawn all threads.
    for (size_t i = 0, n = init->ThreadCount; i < n; ++i)
    {
        OS_WORKER_THREAD_INIT winit = {};
        HANDLE                whand = NULL;
        HANDLE               wready = NULL;
        HANDLE               werror = NULL;
        unsigned int      thread_id = 0;
        const DWORD    THREAD_READY = 0;
        const DWORD    THREAD_ERROR = 1;
        const DWORD      WAIT_COUNT = 2;
        HANDLE      wset[WAIT_COUNT]={};
        DWORD                waitrc = 0;

        // create the manual-reset events signaled by the worker to indicate that it is ready.
        if ((wready = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to create ready signal for worker %Iu of %Iu (0x%08X).\n", __FUNCTION__, tid, i, n, GetLastError());
            goto cleanup_and_fail;
        }
        if ((werror = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to create error signal for worker %Iu of %Iu (0x%08X).\n", __FUNCTION__, tid, i, n, GetLastError());
            CloseHandle(wready);
            goto cleanup_and_fail;
        }

        // populate the OS_WORKER_THREAD_INIT and then spawn the worker thread.
        // the worker thread will need to copy this structure if it wants to access it 
        // past the point where it signals the wready event.
        winit.ThreadPool      = pool;
        winit.ReadySignal     = wready;
        winit.ErrorSignal     = werror;
        winit.LaunchSignal    = evt_launch;
        winit.TerminateSignal = evt_terminate;
        winit.CompletionPort  = iocp;
        winit.ThreadInit      = init->ThreadInit;
        winit.ThreadMain      = init->ThreadMain;
        winit.PoolContext     = init->PoolContext;
        winit.StackSize       = init->StackSize;
        winit.ArenaSize       = init->ArenaSize;
        winit.NUMAGroup       = init->NUMAGroup;
        if ((whand = (HANDLE) _beginthreadex(NULL, (unsigned) init->StackSize, OsWorkerThreadMain, &winit, 0, &thread_id)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to spawn worker %Iu of %Iu (errno = %d).\n", __FUNCTION__, tid, i, n, errno);
            CloseHandle(werror);
            CloseHandle(wready);
            goto cleanup_and_fail;
        }

        // save the various thread attributes in case 
        pool->OSThreadHandle[i]  = whand;
        pool->OSThreadIds[i] = thread_id;
        pool->WorkerReady[i] = wready;
        pool->WorkerError[i] = werror;
        pool->ActiveThreads++;

        // wait for the thread to become ready.
        wset[THREAD_READY] = wready; 
        wset[THREAD_ERROR] = werror;
        if ((waitrc = WaitForMultipleObjects(WAIT_COUNT, wset, FALSE, INFINITE)) != (WAIT_OBJECT_0+THREAD_READY))
        {   // thread initialization failed, or the wait failed.
            // events are already in the OS_THREAD_POOL arrays, so don't clean up here.
            OsLayerError("ERROR: %S(%u): Failed to initialize worker %Iu of %Iu (0x%08X).\n", __FUNCTION__, tid, i, n, waitrc);
            goto cleanup_and_fail;
        }
        // SetThreadGroupAffinity, eventually.
    }

    // everything has been successfully initialized. all worker threads are waiting on the launch signal.
    return 0;

cleanup_and_fail:
    if (pool->ActiveThreads > 0)
    {   // signal all threads to terminate, and then wait until they all die.
        // all workers are blocked waiting on the launch event.
        SetEvent(evt_terminate);
        SetEvent(evt_launch);
        WaitForMultipleObjects((DWORD) pool->ActiveThreads, pool->OSThreadHandle, TRUE, INFINITE);
        // now that all threads have exited, close their handles.
        for (size_t i = 0, n = pool->ActiveThreads; i < n; ++i)
        {
            if (pool->OSThreadHandle != NULL) CloseHandle(pool->OSThreadHandle[i]);
            if (pool->WorkerReady != NULL) CloseHandle(pool->WorkerReady[i]);
            if (pool->WorkerError != NULL) CloseHandle(pool->WorkerError[i]);
        }
    }
    // clean up the task profiler objects.
    if (cv_series) CvReleaseMarkerSeries(cv_series);
    if (cv_provider) CvReleaseProvider(cv_provider);
    // clean up the I/O completion port and synchronization objects.
    if (evt_terminate) CloseHandle(evt_terminate);
    if (evt_launch) CloseHandle(evt_launch);
    if (iocp) CloseHandle(iocp);
    // reset the memory arena back to its initial state.
    OsMemoryArenaResetToMarker(arena, mem_marker);
    // zero out the OS_THREAD_POOL prior to returning to the caller.
    ZeroMemory(pool, sizeof(OS_THREAD_POOL));
    return -1;
}

/// @summary Launch all worker threads in a thread pool, allowing them to execute tasks.
/// @param pool The OS_THREAD_POOL managing the worker threads to launch.
public_function void
OsLaunchThreadPool
(
    OS_THREAD_POOL *pool
)
{
    if (pool->LaunchSignal != NULL)
    {
        SetEvent(pool->LaunchSignal);
    }
}

/// @summary Perform a fast shutdown of a thread pool. The calling thread does not wait for the worker threads to exit. No handles are closed.
/// @param pool The OS_THREAD_POOL to shut down.
public_function void
OsTerminateThreadPool
(
    OS_THREAD_POOL *pool
)
{
    if (pool->ActiveThreads > 0)
    {
        DWORD last_error = ERROR_SUCCESS;
        // signal the termination event prior to waking any waiting threads.
        SetEvent(pool->TerminateSignal);
        // signal all worker threads in the pool. any active processing will complete before this signal is received.
        OsSignalWorkerThreads(pool->CompletionPort, 0, pool->ActiveThreads, last_error);
        // signal the launch event, in case no threads have been launched yet.
        SetEvent(pool->LaunchSignal);
    }
}

/// @summary Perform a complete shutdown and cleanup of a thread pool. The calling thread is blocked until all threads exit.
/// @param pool The OS_THREAD_POOL to shut down and clean up.
public_function void
OsDestroyThreadPool
(
    OS_THREAD_POOL *pool
)
{
    if (pool->ActiveThreads > 0)
    {   
        DWORD last_error = ERROR_SUCCESS;
        // signal the termination event prior to waking any waiting threads.
        SetEvent(pool->TerminateSignal);
        // signal all worker threads in the pool. any active processing will complete before this signal is received.
        OsSignalWorkerThreads(pool->CompletionPort, 0, pool->ActiveThreads, last_error);
        // signal the launch event, in case no threads have been launched yet.
        SetEvent(pool->LaunchSignal);
        // finally, wait for all threads to terminate gracefully.
        WaitForMultipleObjects((DWORD) pool->ActiveThreads, pool->OSThreadHandle, TRUE, INFINITE);
        // now that all threads have exited, close their handles.
        for (size_t i = 0, n = pool->ActiveThreads; i < n; ++i)
        {
            CloseHandle(pool->OSThreadHandle[i]);
            CloseHandle(pool->WorkerReady[i]);
            CloseHandle(pool->WorkerError[i]);
        }
        ZeroMemory(pool->OSThreadIds   , pool->ActiveThreads * sizeof(unsigned int));
        ZeroMemory(pool->OSThreadHandle, pool->ActiveThreads * sizeof(HANDLE));
        ZeroMemory(pool->WorkerReady   , pool->ActiveThreads * sizeof(HANDLE));
        ZeroMemory(pool->WorkerError   , pool->ActiveThreads * sizeof(HANDLE));
        pool->ActiveThreads = 0;
    }
    // clean up the task provider objects from the Concurrency Visualizer SDK.
    if (pool->TaskProfiler.MarkerSeries != NULL)
    {
        CvReleaseMarkerSeries(pool->TaskProfiler.MarkerSeries);
        pool->TaskProfiler.MarkerSeries = NULL;
    }
    if (pool->TaskProfiler.Provider != NULL)
    {
        CvReleaseProvider(pool->TaskProfiler.Provider);
        pool->TaskProfiler.Provider = NULL;
    }
    if (pool->LaunchSignal != NULL)
    {
        CloseHandle(pool->LaunchSignal);
        pool->LaunchSignal = NULL;
    }
    if (pool->TerminateSignal != NULL)
    {
        CloseHandle(pool->TerminateSignal);
        pool->TerminateSignal = NULL;
    }
    if (pool->CompletionPort != NULL)
    {
        CloseHandle(pool->CompletionPort);
        pool->CompletionPort = NULL;
    }
}

/// @summary Send an application-defined signal from one worker thread to one or more other worker threads in the same pool.
/// @param sender The OS_WORKER_THREAD state for the thread sending the signal.
/// @param signal_arg The application-defined data to send as the signal.
/// @param thread_count The number of waiting threads to signal.
/// @return true if the specified number of signal notifications were successfully posted to the thread pool.
public_function bool
OsSignalWorkerThreads
(
    OS_WORKER_THREAD *sender, 
    uintptr_t     signal_arg, 
    size_t      thread_count 
)
{
    DWORD last_error = ERROR_SUCCESS;
    if  (!OsSignalWorkerThreads(sender->CompletionPort, signal_arg, thread_count, last_error))
    {
        OsLayerError("ERROR: %S(%u): Signaling worker threads failed with result 0x%08X.\n", __FUNCTION__, sender->ThreadId, last_error);
        return false;
    }
    return true;
}

/// @summary Send an application-defined signal to wake one or more worker threads in a thread pool.
/// @param pool The thread pool to signal.
/// @param signal_arg The application-defined data to send as the signal.
/// @param thread_count The number of waiting threads to signal.
/// @return true if the specified number of signal notifications were successfully posted to the thread pool.
public_function bool
OsSignalWorkerThreads
(
    OS_THREAD_POOL *pool, 
    uintptr_t signal_arg, 
    size_t  thread_count
)
{
    DWORD last_error = ERROR_SUCCESS;
    if  (!OsSignalWorkerThreads(pool->CompletionPort, signal_arg, thread_count, last_error))
    {
        OsLayerError("ERROR: %S(%u): Signaling worker pool failed with result 0x%08X.\n", __FUNCTION__, GetCurrentThreadId(), last_error);
        return false;
    }
    return true;
}

/// @summary Resets the state of the low-level input system.
/// @param system A pointer to the low-level input system to reset.
public_function void
OsResetInputSystem
(
    OS_INPUT_SYSTEM *system
)
{
    system->LastPollTime = 0;
    system->PrevPortIds  = 0;
    system->CurrPortIds  = 0;

    system->BufferIndex  = 0;
    system->KeyboardBuffer[0] = OS_KEYBOARD_LIST_STATIC_INIT;
    system->KeyboardBuffer[1] = OS_KEYBOARD_LIST_STATIC_INIT;
    system->PointerBuffer [0] = OS_POINTER_LIST_STATIC_INIT;
    system->PointerBuffer [1] = OS_POINTER_LIST_STATIC_INIT;
    system->GamepadBuffer [0] = OS_GAMEPAD_LIST_STATIC_INIT;
    system->GamepadBuffer [1] = OS_GAMEPAD_LIST_STATIC_INIT;
}

/// @summary Push a Raw Input device packet into the input system.
/// @param system The low-level input system to update.
/// @param input The Raw Input device packet.
public_function void
OsPushRawInput
(
    OS_INPUT_SYSTEM *system,
    RAWINPUT const   *input
)
{
    size_t  buffer_index = system->BufferIndex & 1;
    switch (input->header.dwType)
    {
        case RIM_TYPEMOUSE:
            {   // mouse devices are supported; update the device state.
                OsProcessPointerPacket(input, &system->PointerBuffer[buffer_index]);
            } break;

        case RIM_TYPEKEYBOARD:
            {   // keyboard devices are supported; update the device state.
                OsProcessKeyboardPacket(input, &system->KeyboardBuffer[buffer_index]);
            } break;

        default:
            {   // unsupported device type; ignore the input packet.
            } break;
    }
}

/// @summary Push a Raw Input device change notification to the input system when a device is attached or removed.
/// @param system The low-level input system to update.
/// @param wparam The WPARAM value from the WM_INPUT_DEVICE_CHANGE message, specifying whether a device was attached or removed.
/// @param lparam The LPARAM value from the WM_INPUT_DEVICE_CHANGE message, specifying the device HANDLE.
public_function void
OsPushRawInputDeviceChange
(
    OS_INPUT_SYSTEM *system, 
    WPARAM           wparam, 
    LPARAM           lparam
)
{
#ifndef RIDI_DEVICEINFO
    UINT const      RIDI_DEVICEINFO = 0x2000000bUL;
#endif
    RID_DEVICE_INFO device_info     = {};
    UINT            info_size       = sizeof(RID_DEVICE_INFO);
    size_t          curr_buffer     = system->BufferIndex & 1;
    HANDLE          hDevice         = (HANDLE) lparam;

    device_info.cbSize = sizeof(RID_DEVICE_INFO);
    if (GetRawInputDeviceInfo(hDevice, RIDI_DEVICEINFO, &device_info, &info_size) <= info_size)
    {   // the device information was successfully retrieved.
        if (wparam == GIDC_ARRIVAL)
        {   // what type of device was just attached?
            if (device_info.dwType == RIM_TYPEMOUSE)
            {
                OS_POINTER_STATE state  = OS_POINTER_STATE_STATIC_INIT;
                OsDeviceAttached(&system->PointerBuffer[curr_buffer], hDevice, state);
            }
            else if (device_info.dwType == RIM_TYPEKEYBOARD)
            {
                OS_KEYBOARD_STATE state = OS_KEYBOARD_STATE_STATIC_INIT;
                OsDeviceAttached(&system->KeyboardBuffer[curr_buffer], hDevice, state);
            }
        }
        else if (wparam == GIDC_REMOVAL)
        {   // what type of device was just removed?
            if (device_info.dwType == RIM_TYPEMOUSE)
            {
                OsDeviceRemoved(&system->PointerBuffer[curr_buffer], hDevice);
            }
            else if (device_info.dwType == RIM_TYPEKEYBOARD)
            {
                OsDeviceRemoved(&system->KeyboardBuffer[curr_buffer], hDevice);
            }
        }
    }
    else
    {   // typically when a device is removed, GetRawInputDeviceInfo fails.
        if (wparam == GIDC_REMOVAL)
        {   // not much can be done aside from guessing. try keyboards first.
            if (!OsDeviceRemoved(&system->KeyboardBuffer[curr_buffer], hDevice))
            {   // not a keyboard, so try pointer devices next.
                OsDeviceRemoved(&system->PointerBuffer[curr_buffer], hDevice);
            }
        }
    }
}

/// @summary Simulate a key press event; useful for input playback.
/// @param system The low-level input system to update.
/// @param device The system keyboard device identifier, or INVALID_HANDLE_VALUE to broadcast to all devices.
/// @param virtual_key One of the VK_* virtual key constants specifying the key being pressed.
public_function void
OsSimulateKeyPress
(
    OS_INPUT_SYSTEM *system, 
    HANDLE           device, 
    UINT        virtual_key
)
{
    OS_KEYBOARD_LIST *buffer = &system->KeyboardBuffer[system->BufferIndex & 1];
    if (device != INVALID_HANDLE_VALUE)
    {   // broadcast the event to a single device only.
        for (size_t i = 0, n = buffer->DeviceCount; i < n; ++i)
        {
            if (buffer->DeviceHandle[i] == device)
            {   // set the bit corresponding to the virtual key code.
                buffer->DeviceState[i].KeyState[virtual_key >> 5] |= (1UL << (virtual_key & 0x1F));
                break;
            }
        }
    }
    else
    {   // broadcast the event to all attached devices.
        for (size_t i = 0, n = buffer->DeviceCount; i < n; ++i)
        {   // set the bit corresponding to the virtual key code.
            buffer->DeviceState[i].KeyState[virtual_key >> 5] |= (1UL << (virtual_key & 0x1F));
        }
    }
}

/// @summary Simulate a key release event; useful for input playback.
/// @param system The low-level input system to update.
/// @param device The system keyboard device identifier, or INVALID_HANDLE_VALUE to broadcast to all devices.
/// @param virtual_key One of the VK_* virtual key constants specifying the key being released.
public_function void
OsSimulateKeyRelease
(
    OS_INPUT_SYSTEM *system,
    HANDLE           device,
    UINT        virtual_key
)
{
    OS_KEYBOARD_LIST *buffer = &system->KeyboardBuffer[system->BufferIndex & 1];
    if (device != INVALID_HANDLE_VALUE)
    {   // broadcast the event to a single device only.
        for (size_t i = 0, n = buffer->DeviceCount; i < n; ++i)
        {
            if (buffer->DeviceHandle[i] == device)
            {   // clear the bit corresponding to the virtual key code.
                buffer->DeviceState[i].KeyState[virtual_key >> 5] &= ~(1UL << (virtual_key & 0x1F));
                break;
            }
        }
    }
    else
    {   // broadcast the event to all attached devices.
        for (size_t i = 0, n = buffer->DeviceCount; i < n; ++i)
        {   // clear the bit corresponding to the virtual key code.
            buffer->DeviceState[i].KeyState[virtual_key >> 5] &= ~(1UL << (virtual_key & 0x1F));
        }
    }
}

/// @summary Retrieve all input device actions and events that have occurred since the most recent call to OsConsumeInputEvents.
/// @param events The input device event data to populate.
/// @param system The low-level input system to query.
/// @param tick_time The timestamp value of the current tick.
public_function void
OsConsumeInputEvents
(
    OS_INPUT_EVENTS *events,
    OS_INPUT_SYSTEM *system,
    uint64_t      tick_time
)
{   // save the current and previous buffer indices at the start of the tick.
    // increment the buffer index to swap the buffers.
    size_t   curr_buffer = system->BufferIndex & 1;
    size_t   prev_buffer = 1 - curr_buffer;
    system->PrevPortIds  = system->CurrPortIds;
    system->BufferIndex++;

    // all keyboard and pointer input has been pushed to the input system already.
    // the gamepads have not yet been polled for their current state.
    // basically, we want to poll gamepads, generate events for all devices, and then swap buffers.
    if (OsElapsedNanoseconds(system->LastPollTime, tick_time) >= OsMillisecondsToNanoseconds(1000))
    {   // poll all gamepad ports to detect any recently plugged-in controllers.
        OsPollGamepads(&system->GamepadBuffer[curr_buffer], OS_ALL_GAMEPAD_PORTS, system->CurrPortIds);
        system->LastPollTime = tick_time;
    }

    // generate the events for all keyboards, pointers and gamepads.
    OsGenerateKeyboardEvents(events, &system->KeyboardBuffer[prev_buffer], &system->KeyboardBuffer[curr_buffer]);
    OsGeneratePointerEvents (events, &system->PointerBuffer [prev_buffer], &system->PointerBuffer [curr_buffer]);
    OsGenerateGamepadEvents (events, &system->GamepadBuffer [prev_buffer], &system->GamepadBuffer [curr_buffer]);

    // re-initialize the new 'current' buffer to the default state.
    OsForwardKeyboardBuffer(&system->KeyboardBuffer[prev_buffer], &system->KeyboardBuffer[curr_buffer]);
    OsForwardPointerBuffer (&system->PointerBuffer [prev_buffer], &system->PointerBuffer [curr_buffer]);
    OsForwardGamepadBuffer (&system->GamepadBuffer [prev_buffer], &system->GamepadBuffer [curr_buffer]);
}

/// @summary Enumerate the Vulkan drivers installed on the host. A host with no Vulkan drivers installed will have a return value of 0 and *icd_count = 0.
/// @param icd_list The list of Vulkan ICD information to populate. Memory is allocated using malloc. This value may be NULL if max_icds is 0.
/// @param max_icds The maximum number of ICD information structures to write to @a icd_list.  This value must be 0 if icd_list is NULL.
/// @param icd_count On return, the number of ICDs available on the system is stored in this location.
/// @return Zero if enumeration completed successfully, or -1 if an error occurred.
public_function int
OsEnumerateVulkanDrivers
(
    OS_VULKAN_ICD_INFO *icd_list, 
    size_t              max_icds, 
    size_t            *icd_count
)
{
    DWORD const MAX_VALUE_NAME = 32768;
    DWORD          value_count = 0;
    WCHAR           *value_buf = NULL;
    size_t            num_icds = 0;
    HKEY                   key = NULL;
    LONG                   res = ERROR_SUCCESS;

    if ((res = RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Khronos\\Vulkan\\Drivers"), 0, KEY_READ, &key)) != ERROR_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to open registry key HKLM\\SOFTWARE\\Khronos\\Vulkan\\Drivers (%08X).\n", __FUNCTION__, GetCurrentThreadId(), res);
        goto cleanup_and_fail;
    }
    if ((value_buf = (WCHAR*) malloc(MAX_VALUE_NAME * sizeof(WCHAR))) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate 64KB buffer for registry value name.\n", __FUNCTION__, GetCurrentThreadId());
        goto cleanup_and_fail;
    }
    for ( ; ; )
    {
        DWORD nv = MAX_VALUE_NAME - 1;
        DWORD nd =(DWORD) sizeof(DWORD);
        DWORD dt = 0;
        DWORD dv = 0;
        if ((res = RegEnumValueW(key, value_count, value_buf, &nv, NULL, &dt, (LPBYTE) &dv, &nd)) != ERROR_SUCCESS)
        {
            if (res == ERROR_NO_MORE_ITEMS)
                break;
            OsLayerError("ERROR: %S(%u): Retrieving Vulkan ICD registry value failed (%08X).\n", __FUNCTION__, GetCurrentThreadId(), res);
            goto cleanup_and_fail;
        }
        if (dt == REG_DWORD && dv == 0 && nd == (DWORD) sizeof(DWORD))
        {   // this appears to be a valid JSON manifest value. can the manifest file be opened?
            HANDLE json_fd = CreateFileW(value_buf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            if (json_fd != INVALID_HANDLE_VALUE)
            {   // the manifest file was successfully opened.
                if (num_icds < max_icds && icd_list != NULL)
                {
                    LARGE_INTEGER json_size = {};
                    uint8_t      *json_data = NULL;
                    size_t        path_size = wcslen(value_buf) + 1;
                    WCHAR        *json_path = NULL;
                    WCHAR      *driver_path = NULL;
                    char          *lib_path = NULL;
                    char          *api_vers = NULL;
                    char           *lib_end = NULL;
                    char           *api_end = NULL;
                    DWORD            nbread = 0;
                    int              ver_mj = 0;
                    int              ver_mi = 0;
                    int              ver_pn = 0;
                    int        driver_chars = 0;

                    // zero out the ICD info structure.
                    ZeroMemory(&icd_list[num_icds], sizeof(OS_VULKAN_ICD_INFO));

                    if (!GetFileSizeEx(json_fd, &json_size))
                    {   // unable to retrieve the manifest file size, skip this ICD.
                        OsLayerError("ERROR: %S(%u): Unable to retrieve Vulkan ICD JSON manifest size for \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), value_buf, GetLastError());
                        CloseHandle(json_fd);
                        continue;
                    }
                    if ((json_data = (uint8_t*) malloc((json_size.QuadPart + 4) * sizeof(uint8_t))) == NULL)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to allocate %I64d bytes for Vulkan ICD JSON manifest \"%s\".\n", __FUNCTION__, GetCurrentThreadId(), (json_size.QuadPart+4)*sizeof(char), value_buf);
                        CloseHandle(json_fd);
                        continue;
                    }
                    if ((json_path = (WCHAR*) malloc(path_size * sizeof(WCHAR))) == NULL)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan ICD JSON manifest path \"%s\".\n", __FUNCTION__, GetCurrentThreadId(), value_buf);
                        CloseHandle(json_fd);
                        free(json_data);
                        continue;
                    }
                    if (!ReadFile(json_fd, json_data, (DWORD) json_size.QuadPart, &nbread, NULL))
                    {
                        OsLayerError("ERROR: %S(%u): Unable to read JSON data for Vulkan ICD manifest \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), value_buf, GetLastError());
                        CloseHandle(json_fd);
                        free(json_path);
                        free(json_data);
                        continue;
                    }
                    // ensure the data is zero-terminated so it can be used with string searching functions.
                    // since the file encoding is unknown, ensure the string is terminated with four zero-bytes (in case of UTF-32).
                    json_data[json_size.QuadPart+0] = 0;
                    json_data[json_size.QuadPart+1] = 0;
                    json_data[json_size.QuadPart+2] = 0;
                    json_data[json_size.QuadPart+3] = 0;
                    // copy the manifest path string over to the user buffer.
                    CopyMemory(json_path, value_buf, (nv+1) * sizeof(WCHAR));
                    // parse out the necessary values from the JSON manifest.
                    // for now, assume ASCII or UTF-8 encoding is used for the manifest.
                    // some manifests have whitespace after the key but before the ':'.
                    if ((lib_path = (char*) OsStringSearch((char const*) json_data, "\"library_path\"")) == NULL)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to find ICD \"library_path\" in Vulkan ICD manifest \"%s\".\n", __FUNCTION__, GetCurrentThreadId(), value_buf);
                        CloseHandle(json_fd);
                        free(json_path);
                        free(json_data);
                        continue;
                    }
                    if ((api_vers = (char*) OsStringSearch((char const*) lib_path, "\"api_version\"")) == NULL)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to find ICD \"api_version\" in Vulkan ICD manifest \"%s\".\n", __FUNCTION__, GetCurrentThreadId(), value_buf);
                        CloseHandle(json_fd);
                        free(json_path);
                        free(json_data);
                        continue;
                    }
                    // skip the keys, then find the start of the values.
                    lib_path += strlen("\"library_path\":");
                    api_vers += strlen("\"api_version\":");
                    while (*lib_path && *lib_path != '\"')
                        lib_path++;
                    while (*api_vers && *api_vers != '\"')
                        api_vers++;
                    // skip the leading double-quotes.
                    lib_end = ++lib_path; 
                    api_end = ++api_vers;
                    // find the end of the values.
                    while (*lib_end && *lib_end != '\"')
                        lib_end++;
                    while (*api_end && *api_end != '\"')
                        api_end++;
                    // allocate a user buffer for the library path, converted to UTF-16.
                    if ((driver_chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, lib_path, (int)(lib_end-lib_path), NULL, 0)) == 0)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to convert ICD driver path in Vulkan ICD manifest \"%s\" to UTF-16 (%08X).\n", __FUNCTION__, GetCurrentThreadId(), value_buf, GetLastError());
                        CloseHandle(json_fd);
                        free(json_path);
                        free(json_data);
                        continue;
                    }
                    if ((driver_path = (WCHAR*) malloc((driver_chars+1) * sizeof(WCHAR))) == NULL)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to allocate memory for ICD driver path in Vulkan ICD manifest \"%s\".\n", __FUNCTION__, GetCurrentThreadId(), value_buf);
                        CloseHandle(json_fd);
                        free(json_path);
                        free(json_data);
                        continue;
                    }
                    if (MultiByteToWideChar(CP_UTF8, 0, lib_path, (int)(lib_end-lib_path), driver_path, driver_chars) != driver_chars)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to convert ICD driver path in Vulkan ICD manifest \"%s\" to UTF-16 (%08X).\n", __FUNCTION__, GetCurrentThreadId(), value_buf, GetLastError());
                        CloseHandle(json_fd);
                        free(driver_path);
                        free(json_path);
                        free(json_data);
                        continue;
                    }
                    // ensure the path is zero-terminated.
                    driver_path[driver_chars]  = 0;
                    #pragma warning(push)
                    #pragma warning(disable:4996) // Consider using sscanf_s instead.
                    // parse out the Vulkan API version.
                    sscanf(api_vers, "%d.%d.%d", &ver_mj, &ver_mi, &ver_pn);
                    #pragma warning(pop)
                    icd_list[num_icds].ManifestPath = json_path;
                    icd_list[num_icds].ManifestData = json_data;
                    icd_list[num_icds].DriverPath   = driver_path;
                    icd_list[num_icds].MajorVersion =(uint32_t) ver_mj;
                    icd_list[num_icds].MinorVersion =(uint32_t) ver_mi;
                    icd_list[num_icds].PatchVersion =(uint32_t) ver_pn;
                }
                CloseHandle(json_fd);
                num_icds++;
            }
        }

        // retrieve the next value under the registry key.
        ++value_count;
    }

    // cleanup and return.
    if (icd_count != NULL) *icd_count = num_icds;
    RegCloseKey(key);
    free(value_buf);
    return 0;

cleanup_and_fail:
    if (value_buf != NULL) free(value_buf);
    if (key != NULL) RegCloseKey(key);
    if (icd_list != NULL) ZeroMemory(icd_list, max_icds * sizeof(OS_VULKAN_ICD_INFO));
    if (icd_count != NULL) *icd_count = 0;
    return -1;
}

/// @summary Free memory allocated for a Vulkan ICD records populated by OsEnumerateVulkanDrivers. The ICD list itself is not freed.
/// @param icd_list The Vulkan ICD list populated by OsEnumerateVulkanDrivers.
/// @param icd_count The number of ICD records populated by OsEnumerateVulkanDrivers.
public_function void
OsFreeVulkanDriverList
(
    OS_VULKAN_ICD_INFO *icd_list, 
    size_t             icd_count
)
{
    if (icd_list != NULL)
    {
        for (size_t i = 0; i < icd_count; ++i)
        {
            free(icd_list[i].ManifestPath);
            free(icd_list[i].ManifestData);
            free(icd_list[i].DriverPath);
            ZeroMemory(&icd_list[i], sizeof(OS_VULKAN_ICD_INFO));
        }
    }
}

/// @summary Attempt to resolve Vulkan exports and global functions used to create a Vulkan instance or enumerate runtime layers and extensions from an ICD module.
/// @param runtime The Vulkan runtime instance to populate with function pointers.
/// @param icd_module The handle to the Vulkan driver or runtime module.
/// @return VK_SUCCESS if Vulkan is available on the host system.
public_function VkResult
OsLoadVulkanDriver
(
    OS_VULKAN_RUNTIME_DISPATCH *runtime, 
    HMODULE                  icd_module
)
{
    VkResult result = VK_SUCCESS;

    // initialize all of the fields of the runtime loader instance.
    ZeroMemory(runtime, sizeof(OS_VULKAN_RUNTIME_DISPATCH));

    // initialize the base loader object fields. this is required prior to resolving additional entry points.
    runtime->LoaderHandle = icd_module;

    // resolve the core Vulkan loader entry points.
    if ((result = OsResolveVulkanExportFunctions(runtime)) != VK_SUCCESS)
    {   // the loader will be unable to load additional required entry points or create an instance.
        ZeroMemory(runtime, sizeof(OS_VULKAN_RUNTIME_DISPATCH));
        return result;
    }
    if ((result = OsResolveVulkanGlobalFunctions(runtime)) != VK_SUCCESS)
    {   // the loader will be unable to create an instance or enumerate devices.
        ZeroMemory(runtime, sizeof(OS_VULKAN_RUNTIME_DISPATCH));
        return result;
    }

    return VK_SUCCESS;
}

/// @summary Attempt to resolve Vulkan exports and global functions used to create a Vulkan instance or enumerate runtime layers and extensions from an ICD module.
/// @param runtime The Vulkan runtime instance to populate with function pointers.
/// @param icd_info A description of the Vulkan client driver to load.
/// @return VK_SUCCESS if Vulkan is available on the host system.
public_function VkResult
OsLoadVulkanIcd
(
    OS_VULKAN_RUNTIME_DISPATCH *runtime, 
    OS_VULKAN_ICD_INFO        *icd_info
)
{
    HMODULE icd_instance = NULL;
    VkResult      result = VK_SUCCESS;

    if ((icd_instance = LoadLibraryW(icd_info->DriverPath)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to load Vulkan ICD module from \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), icd_info->DriverPath, GetLastError());
        ZeroMemory(runtime, sizeof(OS_VULKAN_RUNTIME_DISPATCH));
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if ((result = OsLoadVulkanDriver(runtime, icd_instance)) != VK_SUCCESS)
    {
        ZeroMemory(runtime, sizeof(OS_VULKAN_RUNTIME_DISPATCH));
        CloseHandle(icd_instance);
        return result;
    }
    return VK_SUCCESS;
}

/// @summary Attempt to load the LunarG Vulkan runtime library and resolve all exported and global functions used to create a Vulkan instance or enumerate runtime layers and extensions.
/// @param runtime The Vulkan runtime instance to populate with function pointers.
/// @return VK_SUCCESS if Vulkan is available on the host system.
public_function VkResult
OsLoadVulkanRuntime
(
    OS_VULKAN_RUNTIME_DISPATCH *runtime
)
{
    HMODULE vrt_instance = NULL;
    VkResult      result = VK_SUCCESS;

    // attempt to load the LunarG Vulkan loader into the process address space.
    if ((vrt_instance = LoadLibrary(_T("vulkan-1.dll"))) == NULL)
    {   // if the LunarG loader isn't available, assume no ICD is present.
        OsLayerError("ERROR: %S(%u): Unable to load LunarG Vulkan runtime module \"vulkan-1.dll\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), GetLastError());
        ZeroMemory(runtime, sizeof(OS_VULKAN_RUNTIME_DISPATCH));
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if ((result = OsLoadVulkanDriver(runtime, vrt_instance)) != VK_SUCCESS)
    {
        ZeroMemory(runtime, sizeof(OS_VULKAN_RUNTIME_DISPATCH));
        CloseHandle(vrt_instance);
        return result;
    }
    return VK_SUCCESS;
}

/// @summary Retrieve the validation layers and extensions supported by the Vulkan runtime,
/// @param props The runtime properties structure to populate.
/// @param runtime A valid OS_VULKAN_RUNTIME_DISPATCH structure with global function pointers set.
/// @param arena The memory arena to use to allocate memory for the runtime property data.
/// @return VK_SUCCESS if the operation is successful.
public_function VkResult
OsQueryVulkanRuntimeProperties
(
    OS_VULKAN_RUNTIME_PROPERTIES *props, 
    OS_VULKAN_RUNTIME_DISPATCH *runtime, 
    OS_MEMORY_ARENA              *arena
)
{
    uint32_t     layer_count = 0;
    uint32_t layer_ext_count = 0;
    uint32_t extension_count = 0;
    VkResult          result = VK_SUCCESS;
    os_arena_marker_t marker = OsMemoryArenaMark(arena);

    // initialize all of the fields of the runtime properties instance.
    ZeroMemory(props, sizeof(OS_VULKAN_RUNTIME_PROPERTIES));

    // enumerate supported instance-level layers and extensions. get the count first, followed by the data.
    if ((result = runtime->vkEnumerateInstanceLayerProperties(&layer_count, NULL)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the number of Vulkan instance layers exposed by the runtime (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }
    if ((result = runtime->vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the number of Vulkan instance extensions exposed by the runtime (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }
    if (layer_count != 0)
    {   // allocate memory for and retrieve layer and layer extension information.
        props->LayerCount                = layer_count;
        props->LayerProperties           = OsMemoryArenaAllocateArray<VkLayerProperties     >(arena, layer_count);
        props->LayerExtensionCount       = OsMemoryArenaAllocateArray<size_t                >(arena, layer_count);
        props->LayerExtensionProperties  = OsMemoryArenaAllocateArray<VkExtensionProperties*>(arena, layer_count);
        if (props->LayerProperties == NULL || props->LayerExtensionCount == NULL || props->LayerExtensionProperties == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan instance layer and extension information.\n", __FUNCTION__, GetCurrentThreadId());
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto cleanup_and_fail;
        }
        if ((result = runtime->vkEnumerateInstanceLayerProperties(&layer_count, props->LayerProperties)) < 0)
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve the set of Vulkan instance layers exposed by the runtime (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            goto cleanup_and_fail;
        }
        for (size_t i = 0, n = props->LayerCount; i < n; ++i)
        {
            if ((result = runtime->vkEnumerateInstanceExtensionProperties(props->LayerProperties[i].layerName, &layer_ext_count, NULL)) < 0)
            {
                OsLayerError("ERROR: %S(%u): Unable to retrieve the number of Vulkan instance layer extensions exposed by runtime layer %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), props->LayerProperties[i].layerName, result);
                goto cleanup_and_fail;
            }
            if (layer_ext_count != 0)
            {   // allocate memory for and retrieve the layer extension information.
                props->LayerExtensionCount[i] = layer_ext_count;
                if ((props->LayerExtensionProperties[i] = OsMemoryArenaAllocateArray<VkExtensionProperties>(arena, layer_ext_count)) == NULL)
                {
                    OsLayerError("ERROR: %S(%u): Unable to allocate memory for extensions exposed by Vulkan runtime layer %S.\n", __FUNCTION__, GetCurrentThreadId(), props->LayerProperties[i].layerName);
                    result = VK_ERROR_OUT_OF_HOST_MEMORY;
                    goto cleanup_and_fail;
                }
                if ((result = runtime->vkEnumerateInstanceExtensionProperties(props->LayerProperties[i].layerName, &layer_ext_count, props->LayerExtensionProperties[i])) < 0)
                {
                    OsLayerError("ERROR: %S(%u): Unable to retrieve the set of extensions exposed by Vulkan runtime layer %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), props->LayerProperties[i].layerName, result);
                    goto cleanup_and_fail;
                }
            }
            else
            {   // don't allocate memory for zero-size arrays.
                props->LayerExtensionCount[i] = 0;
                props->LayerExtensionProperties[i] = NULL;
            }
        }
    }
    else
    {   // don't allocate memory for zero-size arrays.
        props->LayerCount = 0;
        props->LayerProperties = NULL;
        props->LayerExtensionCount = NULL;
        props->LayerExtensionProperties = NULL;
    }
    if (extension_count != 0)
    {   // allocate memory for and retrieve the extension information.
        props->ExtensionCount = extension_count;
        if ((props->ExtensionProperties = OsMemoryArenaAllocateArray<VkExtensionProperties>(arena, extension_count)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan instance extension list.\n", __FUNCTION__, GetCurrentThreadId());
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto cleanup_and_fail;
        }
        if ((result = runtime->vkEnumerateInstanceExtensionProperties(NULL, &extension_count, props->ExtensionProperties)) < 0)
        {
            OsLayerError("ERROR: %S(%u): Unable to enumerate Vulkan instance extension properties (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            goto cleanup_and_fail;
        }
    }
    else
    {   // don't allocate memory for zero-size arrays.
        props->ExtensionCount = 0;
        props->ExtensionProperties = NULL;
    }
    return result;

cleanup_and_fail:
    ZeroMemory(props, sizeof(OS_VULKAN_RUNTIME_PROPERTIES));
    OsMemoryArenaResetToMarker(arena, marker);
    return result;
}

/// @summary Create a new Vulkan instance object and resolve instance-level function pointers for the core API and any enabled extensions.
/// @param instance The OS_VULKAN_INSTANCE_DISPATCH to initialize.
/// @param runtime A valid OS_VULKAN_RUNTIME_DISPATCH structure with global function pointers set.
/// @param create_info The VkInstanceCreateInfo to pass to vkCreateInstance.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateInstance.
/// @param result If the function returns OS_VULKAN_LOADER_RESULT_VKERROR, the Vulkan result code is stored at this location.
/// @return One of OS_VULKAN_LOADER_RESULT indicating the result of the operation.
public_function VkResult
OsCreateVulkanInstance
(
    OS_VULKAN_INSTANCE_DISPATCH             *instance,
    OS_VULKAN_RUNTIME_DISPATCH               *runtime, 
    VkInstanceCreateInfo  const          *create_info, 
    VkAllocationCallbacks const *allocation_callbacks
)
{
    VkResult result = VK_SUCCESS;

    // initialize all of the fields of the Vulkan instance.
    ZeroMemory(instance, sizeof(OS_VULKAN_INSTANCE_DISPATCH));

    // create the Vulkan API context (instance) used to enumerate physical devices and extensions.
    if ((result = runtime->vkCreateInstance(create_info, allocation_callbacks, &instance->InstanceHandle)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to create Vulkan instance (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        return result;
    }
    // resolve the instance-level API functions required to enumerate physical devices.
    if ((result = OsResolveVulkanInstanceFunctions(runtime, instance, create_info)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to resolve one or more Vulkan instance-level functions (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        ZeroMemory(instance, sizeof(OS_VULKAN_INSTANCE_DISPATCH));
        return result;
    }

    return VK_SUCCESS;
}

/// @summary Determine if a display is the current primary display attached to the system.
/// @param device_list The list of physical devices and display outputs attached to the system.
/// @param display_index The zero-based index of the display output to query.
/// @return true if the specified display is the primary display.
public_function bool
OsIsPrimaryDisplay
(
    OS_VULKAN_PHYSICAL_DEVICE_LIST const *device_list, 
    size_t                              display_index
)
{
    assert(display_index < device_list->DisplayCount);
    return(device_list->DisplayDevice[display_index].StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
}

/// @summary Return the current refresh rate of a given display.
/// @param device_list The list of physical devices and display outputs attached to the system.
/// @param display_index The zero-based index of the display output to query.
/// @return The display refresh rate, in Hz.
public_function int32_t
OsDisplayRefreshRate
(
    OS_VULKAN_PHYSICAL_DEVICE_LIST const *device_list, 
    size_t                              display_index
)
{   assert(display_index < device_list->DisplayCount);
    DEVMODE     *devmode =&device_list->DisplayMode[display_index];
    if (devmode->dmDisplayFrequency == 0 || devmode->dmDisplayFrequency == 1)
    {   // a value of 0 or 1 indicates the 'default' refresh rate.
        HDC dc = GetDC(NULL);
        int hz = GetDeviceCaps(dc, VREFRESH);
        ReleaseDC(NULL, dc);
        return hz;
    }
    else
    {   // return the display frequency specified in the DEVMODE structure.
        return devmode->dmDisplayFrequency;
    }
}

/// @summary Determine whether an instance-level layer is supported by the runtime.
/// @param props A valid OS_VULKAN_RUNTIME_PROPERTIES to search.
/// @param layer_name A zero-terminated ASCII string specifying the registered name of the layer to locate.
/// @param layer_index If non-NULL, and the named layer is supported, this location is updated with the zero-based index of the layer in the list.
/// @return The layer name string (of the associated VkLayerProperties), if supported; otherwise, NULL.
public_function char const*
OsSupportsVulkanInstanceLayer
(
    OS_VULKAN_RUNTIME_PROPERTIES const      *props,
    char                         const *layer_name, 
    size_t                       *layer_index=NULL
)
{
    return OsSupportsVulkanLayer(layer_name, props->LayerProperties, props->LayerCount, layer_index);
}

/// @summary Determine whether the runtime supports an entire set of instance-level layers.
/// @param props A valid OS_VULKAN_RUNTIME_PROPERTIES to search.
/// @param layer_names An array of zero-terminated ASCII strings specifying the registered name of each layer to validate.
/// @param layer_count The number of strings in the layer_names array.
/// @return true if the runtime supports the entire set of instance-level layers.
public_function bool
OsSupportsAllVulkanInstanceLayers
(
    OS_VULKAN_RUNTIME_PROPERTIES const        *props,
    char                         const **layer_names, 
    size_t                       const   layer_count
)
{
    for (size_t i = 0; i < layer_count; ++i)
    {
        if (OsSupportsVulkanInstanceLayer(props, layer_names[i]) == NULL)
        {
            return false;
        }
    }
    return true;
}

/// @summary Determine whether an instance-level extension is supported by the runtime.
/// @param props A valid OS_VULKAN_RUNTIME_PROPERTIES to search.
/// @param extension_name A zero-terminated ASCII string specifying the registered name of the extension to locate.
/// @param extension_index If non-NULL, and the named extension is supported, this location is updated with the zero-based index of the extension in the list.
/// @return The extension name string (of the associated VkExtensionProperties), if supported; otherwise, NULL.
public_function char const*
OsSupportsVulkanInstanceExtension
(
    OS_VULKAN_RUNTIME_PROPERTIES const          *props,
    char                         const *extension_name, 
    size_t                       *extension_index=NULL
)
{
    return OsSupportsVulkanExtension(extension_name, props->ExtensionProperties, props->ExtensionCount, extension_index);
}

/// @summary Determine whether the runtime supports an entire set of instance-level extensions.
/// @param props A valid OS_VULKAN_RUNTIME_PROPERTIES to search.
/// @param extension_names An array of zero-terminated ASCII strings specifying the registered name of each extension to validate.
/// @param extension_count The number of strings in the extension_names array.
/// @return true if the runtime supports the entire set of instance-level extensions.
public_function bool
OsSupportsAllVulkanInstanceExtensions
(
    OS_VULKAN_RUNTIME_PROPERTIES const            *props,
    char                         const **extension_names, 
    size_t                       const   extension_count
)
{
    for (size_t i = 0; i < extension_count; ++i)
    {
        if (OsSupportsVulkanInstanceExtension(props, extension_names[i]) == NULL)
        {
            return false;
        }
    }
    return true;
}

/// @summary Enumerate all Vulkan-capable physical devices and display outputs on the host.
/// @param device_list The list of device properties to populate.
/// @param instance A valid OS_VULKAN_INSTANCE_DISPATCH structure with instance-level function pointers set.
/// @param arena The memory arena to use to allocate memory for the runtime property data.
/// @param exe_instance The HINSTANCE passed to the WinMain of the application.
/// @return VK_SUCCESS if the operation is successful.
public_function VkResult
OsEnumerateVulkanPhysicalDevices
(
    OS_VULKAN_PHYSICAL_DEVICE_LIST *device_list, 
    OS_VULKAN_INSTANCE_DISPATCH       *instance,
    OS_MEMORY_ARENA                      *arena, 
    HINSTANCE                      exe_instance
)
{
    os_arena_marker_t marker = OsMemoryArenaMark(arena);
    DISPLAY_DEVICE   display = {};
    WNDCLASSEX        wndcls = {};
    VkResult          result = VK_SUCCESS;
    DWORD         req_fields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    uint32_t    device_count = 0;
    size_t     display_count = 0;
    size_t     display_index = 0;

    // initialize all of the fields of the physical device list instance.
    ZeroMemory(device_list, sizeof(OS_VULKAN_PHYSICAL_DEVICE_LIST));
    display.cb = sizeof(DISPLAY_DEVICE);

    // register a window class used during display enumeration.
    if (instance->vkCreateWin32SurfaceKHR != NULL)
    {   // the user has enabled VK_KHR_surface and/or VK_KHR_win32_surface.
        if (!GetClassInfoEx(exe_instance, _T("VkDisplayEnum_Class"), &wndcls))
        {   // the window class has not been registered yet.
            wndcls.cbSize        = sizeof(WNDCLASSEX);
            wndcls.cbClsExtra    = 0;
            wndcls.cbWndExtra    = 0;
            wndcls.hInstance     = exe_instance;
            wndcls.lpszClassName = _T("VkDisplayEnum_Class");
            wndcls.lpszMenuName  = NULL;
            wndcls.lpfnWndProc   = DefWindowProc;
            wndcls.hIcon         = LoadIcon  (0, IDI_APPLICATION);
            wndcls.hIconSm       = LoadIcon  (0, IDI_APPLICATION);
            wndcls.hCursor       = LoadCursor(0, IDC_ARROW);
            wndcls.style         = CS_OWNDC;
            wndcls.hbrBackground = NULL;
            if (!RegisterClassEx(&wndcls))
            {   // unable to register the hidden window class - don't proceed.
                OsLayerError("ERROR: %S(%u): Unable to register Vulkan display enumeration window class (%08X).\n", __FUNCTION__, GetCurrentThreadId(), GetLastError());
                result = VK_ERROR_INITIALIZATION_FAILED;
                goto cleanup_and_fail;
            }
        }
    }

    // count the number of displays attached to the system.
    for (DWORD ordinal = 0; EnumDisplayDevices(NULL, ordinal, &display, 0); ++ordinal)
    {   // ignore pseudo-displays and displays not attached to a desktop.
        if ((display.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0)
            continue;
        if ((display.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
            continue;
        display_count++;
    }

    // query the number of physical devices installed in the system. 
    // allocate memory for the various physical device attribute arrays.
    if ((result = instance->vkEnumeratePhysicalDevices(instance->InstanceHandle, &device_count, NULL)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the number of Vulkan-capable physical devices attached to the host (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }
    device_list->DeviceCount                         = device_count;
    device_list->DeviceHandle                        = OsMemoryArenaAllocateArray<VkPhysicalDevice                >(arena, device_count);
    device_list->DeviceType                          = OsMemoryArenaAllocateArray<VkPhysicalDeviceType            >(arena, device_count);
    device_list->DeviceFeatures                      = OsMemoryArenaAllocateArray<VkPhysicalDeviceFeatures        >(arena, device_count);
    device_list->DeviceProperties                    = OsMemoryArenaAllocateArray<VkPhysicalDeviceProperties      >(arena, device_count);
    device_list->DeviceMemory                        = OsMemoryArenaAllocateArray<VkPhysicalDeviceMemoryProperties>(arena, device_count);
    device_list->DeviceExtensionCount                = OsMemoryArenaAllocateArray<size_t                          >(arena, device_count);
    device_list->DeviceExtensionProperties           = OsMemoryArenaAllocateArray<VkExtensionProperties*          >(arena, device_count);
    device_list->DeviceQueueFamilyCount              = OsMemoryArenaAllocateArray<size_t                          >(arena, device_count);
    device_list->DeviceQueueFamilyProperties         = OsMemoryArenaAllocateArray<VkQueueFamilyProperties*        >(arena, device_count);
    device_list->DeviceQueueFamilyCanPresent         = OsMemoryArenaAllocateArray<VkBool32**                      >(arena, device_count);
    device_list->DeviceCanPresent                    = OsMemoryArenaAllocateArray<VkBool32*                       >(arena, device_count);
    device_list->DeviceSurfaceCapabilities           = OsMemoryArenaAllocateArray<VkSurfaceCapabilitiesKHR*       >(arena, device_count);
    device_list->DeviceSurfaceFormatCount            = OsMemoryArenaAllocateArray<size_t*                         >(arena, device_count);
    device_list->DeviceSurfaceFormats                = OsMemoryArenaAllocateArray<VkSurfaceFormatKHR**            >(arena, device_count);
    device_list->DevicePresentModeCount              = OsMemoryArenaAllocateArray<size_t*                         >(arena, device_count);
    device_list->DevicePresentModes                  = OsMemoryArenaAllocateArray<VkPresentModeKHR**              >(arena, device_count);

    if (device_list->DeviceHandle                   == NULL || 
        device_list->DeviceType                     == NULL || 
        device_list->DeviceFeatures                 == NULL ||
        device_list->DeviceProperties               == NULL || 
        device_list->DeviceMemory                   == NULL || 
        device_list->DeviceExtensionCount           == NULL || 
        device_list->DeviceExtensionProperties      == NULL || 
        device_list->DeviceQueueFamilyCount         == NULL || 
        device_list->DeviceQueueFamilyProperties    == NULL || 
        device_list->DeviceQueueFamilyCanPresent    == NULL || 
        device_list->DeviceCanPresent               == NULL || 
        device_list->DeviceSurfaceCapabilities      == NULL || 
        device_list->DeviceSurfaceFormatCount       == NULL || 
        device_list->DeviceSurfaceFormats           == NULL || 
        device_list->DevicePresentModeCount         == NULL || 
        device_list->DevicePresentModes             == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device properties.\n", __FUNCTION__, GetCurrentThreadId());
        result = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto cleanup_and_fail;
    }
    // retrieve the physical device handles, and then query the runtime for their attributes.
    if ((result = instance->vkEnumeratePhysicalDevices(instance->InstanceHandle, &device_count, device_list->DeviceHandle)) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to enumerate Vulkan physical devices (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }
    for (size_t i = 0, n = device_count; i < n; ++i)
    {
        VkPhysicalDevice   handle = device_list->DeviceHandle[i];
        uint32_t     family_count = 0;
        uint32_t  extension_count = 0;

        instance->vkGetPhysicalDeviceFeatures(handle, &device_list->DeviceFeatures[i]);
        instance->vkGetPhysicalDeviceProperties(handle, &device_list->DeviceProperties[i]);
        instance->vkGetPhysicalDeviceMemoryProperties(handle, &device_list->DeviceMemory[i]);
        instance->vkGetPhysicalDeviceQueueFamilyProperties(handle, &family_count, NULL);
        device_list->DeviceQueueFamilyCount[i] = family_count;
        device_list->DeviceQueueFamilyProperties[i]    = NULL;
        device_list->DeviceQueueFamilyCanPresent[i]    = NULL;
        if (family_count != 0)
        {   // allocate storage for and retrieve queue family information.
            device_list->DeviceQueueFamilyProperties[i] = OsMemoryArenaAllocateArray<VkQueueFamilyProperties>(arena, family_count);
            device_list->DeviceQueueFamilyCanPresent[i] = OsMemoryArenaAllocateArray<VkBool32*>(arena, family_count);
            if (device_list->DeviceQueueFamilyProperties[i] == NULL || device_list->DeviceQueueFamilyCanPresent[i] == NULL)
            {
                OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device queue family properties.\n", __FUNCTION__, GetCurrentThreadId());
                result = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup_and_fail;
            }
            instance->vkGetPhysicalDeviceQueueFamilyProperties(handle, &family_count, device_list->DeviceQueueFamilyProperties[i]);
            ZeroMemory(device_list->DeviceQueueFamilyCanPresent[i], family_count * sizeof(VkBool32*));
            if (display_count > 0)
            {   // allocate storage for and initialize the presentation abilitiy status.
                for (size_t j = 0; j < family_count; ++j)
                {
                    if ((device_list->DeviceQueueFamilyCanPresent[i][j] = OsMemoryArenaAllocateArray<VkBool32>(arena, display_count)) == NULL)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device queue family presentation ability.\n", __FUNCTION__, GetCurrentThreadId());
                        result = VK_ERROR_OUT_OF_HOST_MEMORY;
                        goto cleanup_and_fail;
                    }
                    ZeroMemory(device_list->DeviceQueueFamilyCanPresent[i][j], display_count * sizeof(VkBool32));
                }
            }
        }
        if ((result = instance->vkEnumerateDeviceExtensionProperties(handle, NULL, &extension_count, NULL)) < 0)
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve the number of extensions exposed by Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_list->DeviceProperties[i].deviceName, result);
            goto cleanup_and_fail;
        }
        device_list->DeviceExtensionCount[i] = extension_count;
        device_list->DeviceExtensionProperties[i] = NULL;
        if (extension_count != 0)
        {   // retrieve the device-level extension information.
            if ((device_list->DeviceExtensionProperties[i] = OsMemoryArenaAllocateArray<VkExtensionProperties>(arena, extension_count)) == NULL)
            {
                OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device extension properties.\n", __FUNCTION__, GetCurrentThreadId());
                result = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup_and_fail;
            }
            if ((result = instance->vkEnumerateDeviceExtensionProperties(handle, NULL, &extension_count, device_list->DeviceExtensionProperties[i])) < 0)
            {
                OsLayerError("ERROR: %S(%u): Unable to retrieve extension information for Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_list->DeviceProperties[i].deviceName, result);
                goto cleanup_and_fail;
            }
        }
        // make sure to copy the device type up into the packed array.
        device_list->DeviceType[i] = device_list->DeviceProperties[i].deviceType;
    }

    // zero-out the display list pointers in case no displays are attached.
    ZeroMemory(device_list->DeviceCanPresent         , device_count * sizeof(VkBool32*));
    ZeroMemory(device_list->DeviceSurfaceCapabilities, device_count * sizeof(VkSurfaceCapabilitiesKHR*));
    ZeroMemory(device_list->DeviceSurfaceFormatCount , device_count * sizeof(size_t*));
    ZeroMemory(device_list->DeviceSurfaceFormats     , device_count * sizeof(VkSurfaceFormatKHR**));
    ZeroMemory(device_list->DevicePresentModeCount   , device_count * sizeof(size_t*));
    ZeroMemory(device_list->DevicePresentModes       , device_count * sizeof(VkPresentModeKHR**));

    // enumerate the attached display outputs and determine whether each physical device can present to the display.
    device_list->DisplayCount   = display_count;
    device_list->DisplayMonitor = NULL;
    device_list->DisplayDevice  = NULL;
    device_list->DisplayMode    = NULL;
    if (display_count > 0)
    {   // allocate storage for per-device, per-display data.
        for (size_t i = 0, n = device_list->DeviceCount; i < n; ++i)
        {
            device_list->DeviceCanPresent[i]               = OsMemoryArenaAllocateArray<VkBool32                >(arena, display_count);
            device_list->DeviceSurfaceCapabilities[i]      = OsMemoryArenaAllocateArray<VkSurfaceCapabilitiesKHR>(arena, display_count);
            device_list->DeviceSurfaceFormatCount[i]       = OsMemoryArenaAllocateArray<size_t                  >(arena, display_count);
            device_list->DeviceSurfaceFormats[i]           = OsMemoryArenaAllocateArray<VkSurfaceFormatKHR*     >(arena, display_count);
            device_list->DevicePresentModeCount[i]         = OsMemoryArenaAllocateArray<size_t                  >(arena, display_count);
            device_list->DevicePresentModes[i]             = OsMemoryArenaAllocateArray<VkPresentModeKHR*       >(arena, display_count);
            if (device_list->DeviceCanPresent[i]          == NULL || 
                device_list->DeviceSurfaceCapabilities[i] == NULL || 
                device_list->DeviceSurfaceFormatCount[i]  == NULL || 
                device_list->DeviceSurfaceFormats[i]      == NULL || 
                device_list->DevicePresentModeCount[i]    == NULL || 
                device_list->DevicePresentModes[i]        == NULL)
            {
                OsLayerError("ERROR: %S(%u): Unable to allocate memory for per-device display properties.\n", __FUNCTION__, GetCurrentThreadId());
                result = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup_and_fail;
            }
            // zero out all of the newly allocated storage.
            ZeroMemory(device_list->DeviceCanPresent[i]         , display_count * sizeof(VkBool32));
            ZeroMemory(device_list->DeviceSurfaceCapabilities[i], display_count * sizeof(VkSurfaceCapabilitiesKHR));
            ZeroMemory(device_list->DeviceSurfaceFormatCount[i] , display_count * sizeof(size_t));
            ZeroMemory(device_list->DeviceSurfaceFormats[i]     , display_count * sizeof(VkSurfaceFormatKHR*));
            ZeroMemory(device_list->DevicePresentModeCount[i]   , display_count * sizeof(size_t));
            ZeroMemory(device_list->DevicePresentModes[i]       , display_count * sizeof(VkPresentModeKHR*));
        }

        // allocate storage for the display list.
        device_list->DisplayMonitor = OsMemoryArenaAllocateArray<HMONITOR      >(arena, display_count);
        device_list->DisplayDevice  = OsMemoryArenaAllocateArray<DISPLAY_DEVICE>(arena, display_count);
        device_list->DisplayMode    = OsMemoryArenaAllocateArray<DEVMODE       >(arena, display_count);
        if (device_list->DisplayMonitor == NULL || device_list->DisplayDevice == NULL || device_list->DisplayMode == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate memory for display list.\n", __FUNCTION__, GetCurrentThreadId());
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto cleanup_and_fail;
        }
        ZeroMemory(device_list->DisplayMonitor, display_count * sizeof(HMONITOR));
        ZeroMemory(device_list->DisplayDevice , display_count * sizeof(DISPLAY_DEVICE));
        ZeroMemory(device_list->DisplayMode   , display_count * sizeof(DEVMODE));

        // enumerate attached displays and populate the display list.
        for (DWORD ordinal = 0; EnumDisplayDevices(NULL, ordinal, &display, 0); ++ordinal)
        {   
            DEVMODE            *devmode =&device_list->DisplayMode[display_index];
            VkBool32 compatible_display = VK_TRUE;
            RECT             rc_display = {};
            HWND                 window = NULL;

            // ignore pseudo-displays and displays not attached to a desktop.
            if ((display.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0)
                continue;
            if ((display.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
                continue;
            // it's possible, but unlikely, that a display was attached during enumeration.
            if (display_index == display_count)
                break;

            // copy the display information to the device list.
            CopyMemory(&device_list->DisplayDevice[display_index], &display, display.cb);

            // retrieve the display mode information for the display output.
            devmode->dmSize = sizeof(DEVMODE);
            if (!EnumDisplaySettingsEx(display.DeviceName, ENUM_CURRENT_SETTINGS, devmode, 0))
            {   // fall back to retrieving the settings saved in the registry.
                if (!EnumDisplaySettingsEx(display.DeviceName, ENUM_REGISTRY_SETTINGS, devmode, 0))
                {   // unable to retrieve the display settings - this display is incompatible.
                    compatible_display = VK_FALSE;
                }
            }
            if ((devmode->dmFields & req_fields) != req_fields)
            {   // the required display attributes are not specified - this display is incompatible.
                compatible_display = VK_FALSE;
            }

            // retrieve the monitor handle for the display.
            rc_display.left   = devmode->dmPosition.x;
            rc_display.top    = devmode->dmPosition.y;
            rc_display.right  = devmode->dmPosition.x  + devmode->dmPelsWidth;
            rc_display.bottom = devmode->dmPosition.y  + devmode->dmPelsHeight;
            device_list->DisplayMonitor[display_index] = MonitorFromRect(&rc_display, MONITOR_DEFAULTTONEAREST);

            // create a temporary window on the display.
            if ((window = CreateWindow(_T("VkDisplayEnum_Class"), _T("VkDisplayEnum_Window"), WS_POPUP, rc_display.left, rc_display.top, devmode->dmPelsWidth, devmode->dmPelsHeight, NULL, NULL, exe_instance, NULL)) == NULL)
            {
                OsLayerError("ERROR: %S(%u): Unable to create Vulkan display enumeration window for display %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), display.DeviceName, GetLastError());
                compatible_display = VK_FALSE;
            }

            // for each physical device, create a temporary surface attached to the hidden temporary window.
            // this surface will be used to enumerate the surface formats and presentation modes supported by the device.
            for (size_t i = 0, n = device_list->DeviceCount && compatible_display; i < n; ++i)
            {
                VkPhysicalDevice device = device_list->DeviceHandle[i];
                VkBool32    can_present = VK_FALSE;

                // only attempt to retrieve surface capabilities if the extension is enabled on the instance.
                if (instance->vkCreateWin32SurfaceKHR != NULL)
                {
                    VkSurfaceKHR                    surface = VK_NULL_HANDLE;
                    VkWin32SurfaceCreateInfoKHR create_info = {};

                    // create a temporary surface connecting the physical device to the display window.
                    // if this fails, that's fine; the physical device cannot present to windows on the display.
                    create_info.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                    create_info.pNext     = NULL;
                    create_info.flags     = 0;
                    create_info.hinstance = exe_instance;
                    create_info.hwnd      = window;
                    if (instance->vkCreateWin32SurfaceKHR(instance->InstanceHandle, &create_info, NULL, &surface) == VK_SUCCESS)
                    {   // determine which queue families, if any, can present to the display.
                        for (uint32_t family = 0, family_count = (uint32_t) device_list->DeviceQueueFamilyCount[i]; family < family_count; ++family)
                        {
                            if (instance->vkGetPhysicalDeviceSurfaceSupportKHR(device, family, surface, &device_list->DeviceQueueFamilyCanPresent[i][family][display_index]) != VK_SUCCESS)
                            {   // the call failed, so assume that presentation is not possible.
                                device_list->DeviceQueueFamilyCanPresent[i][family][display_index] = VK_FALSE;
                            }
                            if (device_list->DeviceQueueFamilyCanPresent[i][family][display_index])
                            {   // at least one queue family can present to this display. 
                                can_present = VK_TRUE;
                            }
                        }
                        if (can_present)
                        {   // retrieve the capabilities of the physical device when presenting to the display.
                            uint32_t format_count = 0;
                            uint32_t   mode_count = 0;
                            instance->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &device_list->DeviceSurfaceCapabilities[i][display_index]);
                            instance->vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, NULL);
                            instance->vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, NULL);
                            device_list->DeviceSurfaceFormatCount[i][display_index] = format_count;
                            device_list->DevicePresentModeCount[i][display_index]   = mode_count;
                            device_list->DeviceSurfaceFormats[i][display_index]     = NULL;
                            device_list->DevicePresentModes[i][display_index]       = NULL;
                            if (format_count > 0)
                            {   // retrieve the available surface formats.
                                if ((device_list->DeviceSurfaceFormats[i][display_index] = OsMemoryArenaAllocateArray<VkSurfaceFormatKHR>(arena, format_count)) == NULL)
                                {
                                    OsLayerError("ERROR: %S(%u): Unable to allocate memory for device surface formats.\n", __FUNCTION__, GetCurrentThreadId());
                                    result = VK_ERROR_OUT_OF_HOST_MEMORY;
                                    goto cleanup_and_fail;
                                }
                                instance->vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, device_list->DeviceSurfaceFormats[i][display_index]);
                            }
                            if (mode_count > 0)
                            {   // retrieve the available presentation modes.
                                if ((device_list->DevicePresentModes[i][display_index] = OsMemoryArenaAllocateArray<VkPresentModeKHR>(arena, mode_count)) == NULL)
                                {
                                    OsLayerError("ERROR: %S(%u): Unable to allocate memory for device surface presentation modes.\n", __FUNCTION__, GetCurrentThreadId());
                                    result = VK_ERROR_OUT_OF_HOST_MEMORY;
                                    goto cleanup_and_fail;
                                }
                                instance->vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, device_list->DevicePresentModes[i][display_index]);
                            }
                            if (format_count == 0 || mode_count == 0)
                            {   // the runtime says it can present, but no presentation modes or surface formats are exposed.
                                can_present = VK_FALSE;
                            }
                        }
                        // destroy the temporary surface; it is no longer needed.
                        instance->vkDestroySurfaceKHR(instance->InstanceHandle, surface, NULL);
                    }
                }

                if (compatible_display && can_present)
                {   // at least one queue family from this device can present to the display.
                    device_list->DeviceCanPresent[i][display_index] = VK_TRUE;
                }
                else
                {   // the display is not compatible, or no queue families can present to it.
                    device_list->DeviceCanPresent[i][display_index] = VK_FALSE;
                }
            }

            // finished with the temporary window, so destroy it.
            if (window != NULL)
            {
                DestroyWindow(window);
            }

            // finished working with this display entry.
            display_index++;
        }
        // it's possible, but unlikely, that a display was removed during enumeration.
        if (display_index < display_count)
        {   // in which case, lower the display count.
            device_list->DisplayCount = display_index;
        }
    }

    return VK_SUCCESS;

cleanup_and_fail:
    ZeroMemory(device_list, sizeof(OS_VULKAN_PHYSICAL_DEVICE_LIST));
    OsMemoryArenaResetToMarker(arena, marker);
    return result;
}

/// @summary Create a new Vulkan logical device object and resolve device-level function pointers for the core API and any enabled extensions.
/// @param device The OS_VULKAN_DEVICE_DISPATCH to initialize.
/// @param instance A valid OS_VULKAN_INSTANCE_DISPATCH structure with instance function pointers set.
/// @param physical_device The handle of the Vulkan-capable physical device to associate with the logical device.
/// @param create_info The VkDeviceCreateInfo to pass to vkCreateDevice.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateInstance.
/// @param result If the function returns OS_VULKAN_LOADER_RESULT_VKERROR, the Vulkan result code is stored at this location.
/// @return One of OS_VULKAN_LOADER_RESULT indicating the result of the operation.
public_function VkResult
OsCreateVulkanLogicalDevice
(
    OS_VULKAN_DEVICE_DISPATCH                 *device,
    OS_VULKAN_INSTANCE_DISPATCH             *instance,
    VkPhysicalDevice                  physical_device, 
    VkDeviceCreateInfo    const          *create_info, 
    VkAllocationCallbacks const *allocation_callbacks
)
{
    VkResult result = VK_SUCCESS;

    // initialize all of the fields of the Vulkan logical device.
    ZeroMemory(device, sizeof(OS_VULKAN_DEVICE_DISPATCH));

    // create the Vulkan logical device object.
    if ((result = instance->vkCreateDevice(physical_device, create_info, allocation_callbacks, &device->DeviceHandle)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to create Vulkan device (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        return result;
    }
    // resolve the device-level API functions required to manage resources and submit work to the physical device.
    if ((result = OsResolveVulkanDeviceFunctions(instance, device, create_info)) != VK_SUCCESS)
    {
        OsLayerError("ERROR: %S(%u): Unable to resolve one or more Vulkan device-level functions (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        ZeroMemory(device, sizeof(OS_VULKAN_DEVICE_DISPATCH));
        return result;
    }
    // save the associated physical device handle for later reference.
    device->PhysicalDeviceHandle = physical_device;
    return VK_SUCCESS;
}

/// @summary Initialize the host audio interface and retrieve the default audio device IDs.
/// @param audio_system The audio system interface.
/// @return Zero if the audio system is successfully initialized, or -1 if an error occurs.
public_function int
OsInitializeAudio
(
    OS_AUDIO_SYSTEM *audio_system
)
{
    IMMDeviceEnumerator *devenum = NULL;
    IMMDevice    *default_outdev = NULL;
    IMMDevice    *default_capdev = NULL;
    WCHAR             *outdev_id = NULL;
    WCHAR             *capdev_id = NULL;
    HRESULT               result = S_OK;

    // initialize the fields of the audio system instance.
    ZeroMemory(audio_system, sizeof(OS_AUDIO_SYSTEM));

    // initialize COM on the calling thread. any thread that accesses the audio system must call CoInitializeEx.
    if (FAILED((result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_SPEED_OVER_MEMORY))))
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize audio system COM services (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        return -1;
    }
    // create the multimedia device enumerator and retrieve the IDs of the default output and capture devices.
    if (FAILED((result = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, IID_PPV_ARGS(&devenum)))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the multimedia device enumeration instance (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }
    // retrieve the default audio output device.
    if ((result = devenum->GetDefaultAudioEndpoint(eRender, eConsole, &default_outdev)) != S_OK)
    {   // it's possible that there are no audio output devices. don't fail in that case.
        if (result != E_NOTFOUND)
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve the default audio output device (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            goto cleanup_and_fail;
        }
    }
    // retrieve the default audio capture device.
    if ((result = devenum->GetDefaultAudioEndpoint(eCapture, eConsole, &default_capdev)) != S_OK)
    {   // it's possible that there are no audio capture devices. don't fail in that case.
        if (result != E_NOTFOUND)
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve the default audio capture device (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            goto cleanup_and_fail;
        }
    }

    // retrieve the device ID strings. these are allocated by the COM interface and freed when the audio system is destroyed.
    if ((default_outdev != NULL) && FAILED((result = default_outdev->GetId(&outdev_id))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the system ID of the default audio output device (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }
    if ((default_capdev != NULL) && FAILED((result = default_capdev->GetId(&capdev_id))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the system ID of the default audio output device (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }

    // release the device objects; they are no longer needed.
    if (default_capdev != NULL) default_capdev->Release();
    if (default_outdev != NULL) default_outdev->Release();

    // update the audio system instance.
    audio_system->DeviceEnumerator       = devenum;
    audio_system->DefaultOutputDeviceId  = outdev_id;
    audio_system->DefaultCaptureDeviceId = capdev_id;
    return 0;

cleanup_and_fail:
    ZeroMemory(audio_system, sizeof(OS_AUDIO_SYSTEM));
    if (capdev_id != NULL) CoTaskMemFree(capdev_id);
    if (outdev_id != NULL) CoTaskMemFree(outdev_id);
    if (default_capdev != NULL) default_capdev->Release();
    if (default_outdev != NULL) default_outdev->Release();
    if (devenum != NULL) devenum->Release();
    CoUninitialize();
    return -1;
}

/// @summary Enumerate all audio output and capture devices that are enabled on the host.
/// @param device_list The OS_AUDIO_DEVICE_LIST to populate.
/// @param audio_system The initialized OS_AUDIO_SYSTEM to query.
/// @param arena The OS_MEMORY_ARENA to used when allocating memory for device names and IDs.
/// @return Zero if the operation was successful, or -1 if an error occurred.
public_function int
OsEnumerateAudioDevices
(
    OS_AUDIO_DEVICE_LIST *device_list, 
    OS_AUDIO_SYSTEM     *audio_system, 
    OS_MEMORY_ARENA            *arena
)
{
    IMMDeviceEnumerator     *devenum = audio_system->DeviceEnumerator;
    IMMDeviceCollection *outdev_list = NULL;
    IMMDeviceCollection *capdev_list = NULL;
    os_arena_marker_t         marker = OsMemoryArenaMark(arena);
    HRESULT                   result = S_OK;
    UINT                   out_count = 0;
    UINT                   cap_count = 0;

    // initialize the fields of the device list.
    ZeroMemory(device_list, sizeof(OS_AUDIO_DEVICE_LIST));

    // enumerate attached output audio endpoints.
    // each call provides a list of objects implementing IMMDevice and IMMEndpoint.
    if (FAILED((result = devenum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED, &outdev_list))))
    {
        OsLayerError("ERROR: %S(%u): Unable to enumerate attached audio output devices (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }
    if (FAILED((result = devenum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED, &capdev_list))))
    {
        OsLayerError("ERROR: %S(%u): Unable to enumerate attached audio capture devices (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }
    if (FAILED((result = outdev_list->GetCount(&out_count))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve audio output device count (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }
    if (FAILED((result = capdev_list->GetCount(&cap_count))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve audio capture device count (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        goto cleanup_and_fail;
    }
    device_list->OutputDeviceCount  = out_count;
    device_list->CaptureDeviceCount = cap_count;
    if (out_count > 0)
    {
        device_list->OutputDeviceId      = OsMemoryArenaAllocateArray<WCHAR*>(arena, out_count);
        device_list->OutputDeviceName    = OsMemoryArenaAllocateArray<WCHAR*>(arena, out_count);
        if (device_list->OutputDeviceId == NULL || device_list->OutputDeviceName == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate memory for audio output device information.\n", __FUNCTION__, GetCurrentThreadId());
            goto cleanup_and_fail;
        }
    }
    if (cap_count > 0)
    {
        device_list->CaptureDeviceId     = OsMemoryArenaAllocateArray<WCHAR*>(arena, cap_count);
        device_list->CaptureDeviceName   = OsMemoryArenaAllocateArray<WCHAR*>(arena, cap_count);
        if (device_list->CaptureDeviceId == NULL || device_list->CaptureDeviceName == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate memory for audio capture device information.\n", __FUNCTION__, GetCurrentThreadId());
            goto cleanup_and_fail;
        }
    }

    // loop over the populated collections and retrieve the device information.
    for (UINT i = 0; i < out_count; ++i)
    {
        IMMDevice         *dev = NULL;
        IPropertyStore  *devps = NULL;
        WCHAR           *devid = NULL;
        size_t           idlen = 0;
        size_t           fnlen = 0;
        PROPVARIANT      devnv;
        PropVariantInit(&devnv);

        if (FAILED((result = outdev_list->Item(i, &dev))))
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve audio output device interface (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            goto cleanup_and_fail;
        }
        if (FAILED((result = dev->OpenPropertyStore(STGM_READ, &devps))))
        {
            OsLayerError("ERROR: %S(%u): Unable to open audio output device property store (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            dev->Release();
            goto cleanup_and_fail;
        }
        if (FAILED((result = dev->GetId(&devid))))
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve audio output device ID (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            devps->Release();
            dev->Release();
            goto cleanup_and_fail;
        }
        if (FAILED((result = devps->GetValue(PKEY_Device_FriendlyName, &devnv))))
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve device name for audio output device (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            CoTaskMemFree(devid);
            devps->Release();
            dev->Release();
            goto cleanup_and_fail;
        }
        // note that the lengths returned by StringCchLength do not include the trailing zero character.
        StringCchLengthW(devid        , STRSAFE_MAX_CCH, &idlen);
        StringCchLengthW(devnv.pwszVal, STRSAFE_MAX_CCH, &fnlen);
        if ((device_list->OutputDeviceId  [i] = OsMemoryArenaAllocateArray<WCHAR>(arena, idlen+1)) == NULL ||
            (device_list->OutputDeviceName[i] = OsMemoryArenaAllocateArray<WCHAR>(arena, fnlen+1)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate string memory for audio output device properties.\n", __FUNCTION__, GetCurrentThreadId());
            PropVariantClear(&devnv);
            CoTaskMemFree(devid);
            devps->Release();
            dev->Release();
        }
        StringCchCopyExW(device_list->OutputDeviceId  [i], idlen+1, devid, NULL, NULL, STRSAFE_IGNORE_NULLS);
        StringCchCopyExW(device_list->OutputDeviceName[i], fnlen+1, devnv.pwszVal, NULL, NULL, STRSAFE_IGNORE_NULLS);
        // clean up all of the allocations made by COM.
        PropVariantClear(&devnv);
        CoTaskMemFree(devid);
        devps->Release();
        dev->Release();
    }
    for (UINT i = 0; i < cap_count; ++i)
    {
        IMMDevice         *dev = NULL;
        IPropertyStore  *devps = NULL;
        WCHAR           *devid = NULL;
        size_t           idlen = 0;
        size_t           fnlen = 0;
        PROPVARIANT      devnv;
        PropVariantInit(&devnv);

        if (FAILED((result = capdev_list->Item(i, &dev))))
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve audio capture device interface (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            goto cleanup_and_fail;
        }
        if (FAILED((result = dev->OpenPropertyStore(STGM_READ, &devps))))
        {
            OsLayerError("ERROR: %S(%u): Unable to open audio capture device property store (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            dev->Release();
            goto cleanup_and_fail;
        }
        if (FAILED((result = dev->GetId(&devid))))
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve audio capture device ID (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            devps->Release();
            dev->Release();
            goto cleanup_and_fail;
        }
        if (FAILED((result = devps->GetValue(PKEY_Device_FriendlyName, &devnv))))
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve device name for audio capture device (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
            CoTaskMemFree(devid);
            devps->Release();
            dev->Release();
            goto cleanup_and_fail;
        }
        // note that the lengths returned by StringCchLength do not include the trailing zero character.
        StringCchLengthW(devid        , STRSAFE_MAX_CCH, &idlen);
        StringCchLengthW(devnv.pwszVal, STRSAFE_MAX_CCH, &fnlen);
        if ((device_list->CaptureDeviceId  [i] = OsMemoryArenaAllocateArray<WCHAR>(arena, idlen+1)) == NULL ||
            (device_list->CaptureDeviceName[i] = OsMemoryArenaAllocateArray<WCHAR>(arena, fnlen+1)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate string memory for audio capture device properties.\n", __FUNCTION__, GetCurrentThreadId());
            PropVariantClear(&devnv);
            CoTaskMemFree(devid);
            devps->Release();
            dev->Release();
        }
        StringCchCopyExW(device_list->CaptureDeviceId  [i], idlen+1, devid, NULL, NULL, STRSAFE_IGNORE_NULLS);
        StringCchCopyExW(device_list->CaptureDeviceName[i], fnlen+1, devnv.pwszVal, NULL, NULL, STRSAFE_IGNORE_NULLS);
        // clean up all of the allocations made by COM.
        PropVariantClear(&devnv);
        CoTaskMemFree(devid);
        devps->Release();
        dev->Release();
    }
    
    // release the device collection interfaces, which are no longer needed.
    if (capdev_list != NULL) capdev_list->Release();
    if (outdev_list != NULL) outdev_list->Release();
    return 0;

cleanup_and_fail:
    ZeroMemory(device_list , sizeof(OS_AUDIO_DEVICE_LIST));
    if (capdev_list != NULL) capdev_list->Release();
    if (outdev_list != NULL) outdev_list->Release();
    OsMemoryArenaResetToMarker(arena, marker);
    return -1;
}

/// @summary Disables any active audio output device.
/// @param audio_system The audio system on which the output device will be disabled.
public_function void
OsDisableAudioOutput
(
    OS_AUDIO_SYSTEM *audio_system
)
{
    if (audio_system->AudioOutputEnabled)
    {
        OS_AUDIO_OUTPUT_DEVICE *dev = &audio_system->ActiveOutputDevice;
        if (dev->RenderClient != NULL)
        {
            dev->RenderClient->Release();
            dev->RenderClient = NULL;
        }
        if (dev->AudioClock != NULL)
        {
            dev->AudioClock->Release();
            dev->AudioClock = NULL;
        }
        if (dev->AudioClient != NULL)
        {
            dev->AudioClient->Stop();
            dev->AudioClient->Release();
            dev->AudioClient = NULL;
        }
        if (dev->Device != NULL)
        {
            dev->Device->Release();
            dev->Device = NULL;
        }
        ZeroMemory(dev, sizeof(OS_AUDIO_OUTPUT_DEVICE));
        audio_system->AudioOutputEnabled = false;
    }
}

/// @summary Selects and enables an audio device for audio output.
/// @param audio_system The audio system on which the output device will be enabled.
/// @param device_id A zero-terminated string uniquely identifying the output device to enable.
/// @param samples_per_second The audio data sample rate, in Hertz.
/// @param buffer_size The size of the audio buffer, in samples.
/// @return Zero if the audio output device is enabled, or -1 if an error occurred.
public_function int
OsEnableAudioOutput
(
    OS_AUDIO_SYSTEM    *audio_system,
    WCHAR                 *device_id,
    uint32_t      samples_per_second, 
    uint32_t             buffer_size
)
{
    REFERENCE_TIME  buffer_100ns = 0;
    IMMDeviceEnumerator *devenum = audio_system->DeviceEnumerator;
    IMMDevice               *dev = NULL;
    IAudioClient         *client = NULL;
    IAudioRenderClient *renderer = NULL;
    IAudioClock           *clock = NULL;
    HRESULT               result = S_OK;
    uint32_t         frame_count = 0;
    WAVEFORMATEXTENSIBLE  format = {};

    // retrieve an interface to the audio device, and use that to retrieve an IAudioClient.
    if (FAILED((result = devenum->GetDevice(device_id, &dev))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve audio output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }
    if (FAILED((result = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**) &client))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve IAudioClient interface from output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }

    // set the buffer format and size desired by the application.
    format.Format.cbSize               = sizeof(WAVEFORMATEXTENSIBLE);
    format.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
    format.Format.wBitsPerSample       = 16; // 16-bit audio
    format.Format.nChannels            = 2;  // stereo
    format.Format.nSamplesPerSec       = samples_per_second;
    format.Format.nBlockAlign          =(WORD) (format.Format.nChannels * format.Format.wBitsPerSample / 8);
    format.Format.nAvgBytesPerSec      = format.Format.nSamplesPerSec   * format.Format.nBlockAlign;
    format.Samples.wValidBitsPerSample = 16;
    format.dwChannelMask               = KSAUDIO_SPEAKER_STEREO;
    format.SubFormat                   = KSDATAFORMAT_SUBTYPE_PCM;
    buffer_100ns                       = 10000000ULL * buffer_size / samples_per_second;
    if (FAILED((result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, buffer_100ns, 0, &format.Format, NULL))))
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize the audio client for output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }

    // retrieve the rendering and clock services from the audio client.
    if (FAILED((result = client->GetService(IID_PPV_ARGS(&renderer)))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the IAudioRendererClient for output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }
    if (FAILED((result = client->GetService(IID_PPV_ARGS(&clock)))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the IAudioClock for output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }

    // retrieve the actual size of the audio buffer.
    if (FAILED((result = client->GetBufferSize(&frame_count))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve audio output buffer size for output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }

    // finally, start the audio session.
    if (FAILED((result = client->Start())))
    {
        OsLayerError("ERROR: %S(%u): Unable to start the audio output session for device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }

    // save the necessary data on the output device.
    audio_system->ActiveOutputDevice.Device                   = dev;
    audio_system->ActiveOutputDevice.AudioClient              = client;
    audio_system->ActiveOutputDevice.RenderClient             = renderer;
    audio_system->ActiveOutputDevice.AudioClock               = clock;
    audio_system->ActiveOutputDevice.RequestedBufferSize      = buffer_size;
    audio_system->ActiveOutputDevice.ActualBufferSize         = frame_count;
    audio_system->ActiveOutputDevice.SamplesPerSecond         = samples_per_second;
    CopyMemory(&audio_system->ActiveOutputDevice.AudioFormat  , &format, sizeof(WAVEFORMATEXTENSIBLE));
    StringCchCopyExW(audio_system->ActiveOutputDevice.DeviceId, OS_AUDIO_OUTPUT_DEVICE::MAX_DEVICE_ID, device_id, NULL, NULL, STRSAFE_IGNORE_NULLS);
    audio_system->AudioOutputEnabled = true;
    return 0;

cleanup_and_fail:
    ZeroMemory(&audio_system->ActiveOutputDevice, sizeof(OS_AUDIO_OUTPUT_DEVICE));
    if (clock    != NULL) clock->Release();
    if (renderer != NULL) renderer->Release();
    if (client   != NULL) client->Release();
    if (dev      != NULL) dev->Release();
    return -1;
}

/// @summary Attempt to recover a lost audio output device by destroying the device interfaces and recreating them.
/// @param audio_system The OS_AUDIO_SYSTEM managing the lost output device.
/// @return Zero if the lost device was recovered (or audio output is disabled), or -1 if an error occurred.
public_function int
OsRecoverLostAudioOutputDevice
(
    OS_AUDIO_SYSTEM *audio_system
)
{
    REFERENCE_TIME  buffer_100ns = 0;
    OS_AUDIO_OUTPUT_DEVICE *adev =&audio_system->ActiveOutputDevice;
    IMMDeviceEnumerator *devenum = audio_system->DeviceEnumerator;
    IMMDevice               *dev = NULL;
    IAudioClient         *client = NULL;
    IAudioRenderClient *renderer = NULL;
    IAudioClock           *clock = NULL;
    WCHAR             *device_id = audio_system->ActiveOutputDevice.DeviceId;
    HRESULT               result = S_OK;
    uint32_t         frame_count = 0;

    if (audio_system->AudioOutputEnabled == false)
    {   // no audio output is enabled, so there's nothing to do.
        return 0;
    }

    // release all of the existing interfaces to start from a clean slate.
    if (adev->RenderClient != NULL)
    {
        adev->RenderClient->Release();
        adev->RenderClient = NULL;
    }
    if (adev->AudioClock != NULL)
    {
        adev->AudioClock->Release();
        adev->AudioClock = NULL;
    }
    if (adev->AudioClient != NULL)
    {
        adev->AudioClient->Stop();
        adev->AudioClient->Release();
        adev->AudioClient = NULL;
    }
    if (adev->Device != NULL)
    {
        adev->Device->Release();
        adev->Device = NULL;
    }

    // retrieve an interface to the audio device, and use that to retrieve an IAudioClient.
    if (FAILED((result = devenum->GetDevice(device_id, &dev))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve audio output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }
    if (FAILED((result = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**) &client))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve IAudioClient interface from output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }

    // set the buffer format and size desired by the application.
    buffer_100ns = 10000000ULL * adev->RequestedBufferSize / adev->SamplesPerSecond;
    if (FAILED((result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, buffer_100ns, 0, &adev->AudioFormat.Format, NULL))))
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize the audio client for output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }

    // retrieve the rendering and clock services from the audio client.
    if (FAILED((result = client->GetService(IID_PPV_ARGS(&renderer)))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the IAudioRendererClient for output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }
    if (FAILED((result = client->GetService(IID_PPV_ARGS(&clock)))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the IAudioClock for output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }

    // retrieve the actual size of the audio buffer.
    if (FAILED((result = client->GetBufferSize(&frame_count))))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve audio output buffer size for output device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }

    // finally, start the audio session.
    if (FAILED((result = client->Start())))
    {
        OsLayerError("ERROR: %S(%u): Unable to start the audio output session for device %s (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_id, result);
        goto cleanup_and_fail;
    }

    // save the necessary data on the output device.
    audio_system->ActiveOutputDevice.Device           = dev;
    audio_system->ActiveOutputDevice.AudioClient      = client;
    audio_system->ActiveOutputDevice.RenderClient     = renderer;
    audio_system->ActiveOutputDevice.AudioClock       = clock;
    audio_system->ActiveOutputDevice.ActualBufferSize = frame_count;
    return 0;

cleanup_and_fail:
    if (clock    != NULL) clock->Release();
    if (renderer != NULL) renderer->Release();
    if (client   != NULL) client->Release();
    if (dev      != NULL) dev->Release();
    return -1;
}

/// @summary Query the active audio output device for the number of samples that can be written to the audio buffer.
/// @param audio_system The OS_AUDIO_SYSTEM managing the output device to query.
/// @return The number of samples that can be written to the audio output device.
public_function uint32_t
OsAudioSamplesToWrite
(
    OS_AUDIO_SYSTEM *audio_system
)
{
    IAudioClient *client = audio_system->ActiveOutputDevice.AudioClient;
    HRESULT       result = S_OK;
    uint32_t  pad_frames = 0;
    uint32_t  sample_cnt = 0;
    uint32_t buffer_size = audio_system->ActiveOutputDevice.ActualBufferSize;

    if (audio_system->AudioOutputEnabled == false || client == NULL)
    {   // there's no audio output enabled, so no data can be written.
        return 0;
    }
    if (FAILED((result = client->GetCurrentPadding(&pad_frames))))
    {   // TODO(rlk): handle lost devices, etc.
        return 0;
    }
    // the number of samples to write is buffer_size - pad_frames.
    if ((sample_cnt = buffer_size - pad_frames) > buffer_size)
    {   // clamp to the actual size of the buffer returned by the device.
        sample_cnt = buffer_size;
    }
    return sample_cnt;
}

/// @summary Write audio data to the active output device.
/// @param audio_system The OS_AUDIO_SYSTEM managing the output device.
/// @param sample_data The sample data to write, in 16-bit stereo format.
/// @param sample_count The number of samples to copy from @a sample_data into the device audio buffer.
/// @return Zero if the data was written successfully, or -1 if an error occurred.
public_function int
OsWriteAudioSamples
(   
    OS_AUDIO_SYSTEM *audio_system, 
    void     const   *sample_data,
    uint32_t const    sample_count
)
{
    IAudioRenderClient *client = audio_system->ActiveOutputDevice.RenderClient;
    HRESULT             result = S_OK;
    uint8_t            *buffer = NULL;

    if (audio_system->AudioOutputEnabled == false || client == NULL)
    {   // there's no audio output enabled, so just return.
        return 0;
    }
    if (FAILED((result = client->GetBuffer(sample_count, &buffer))))
    {   // TODO(rlk): handle a lost device here.
        return -1;
    }
    // copy the data to the audio device buffer.
    CopyMemory(buffer, sample_data, 2 * sample_count * sizeof(uint16_t));
    if (FAILED((result = client->ReleaseBuffer(sample_count, 0))))
    {   // TODO(rlk): handle a lost device here.
    }
    return 0;
}

/// @summary Convert a 64-bit UNIX timestamp, specifying the number of seconds elapsed since Jan 1 1970 00:00 UTC, to a Windows FILETIME.
/// @param unix_time_t The source UNIX timestamp value.
/// @return The Windows FILETIME value.
public_function FILETIME
OsUnixTimeToFILETIME
(
    uint64_t unix_time_t
)
{   // 10000000 is the number of 100ns intervals in one second.
    // 116444736000000000 is the number of 100ns intervals between Jan 1 1601 00:00 and Jan 1 1970 00:00 UTC (the epoch difference.)
    FILETIME  filetime;
    ULONGLONG ll = (unix_time_t * 10000000ULL) + 116444736000000000ULL;
    filetime.dwLowDateTime  = (DWORD) ll;
    filetime.dwHighDateTime = (DWORD)(ll >> 32);
}

/// @summary Convert a Windows FILETIME value, specifying the number of 100ns intervals elapsed since Jan 1 1601 00:00 UTC, to a UNIX timestamp value representing the number of seconds elapsed since Jan 1 1970 00:00 UTC.
/// @param filetime The source Windows FILETIME value.
/// @return The UNIX timestamp value.
public_function uint64_t
OsFILETIMEtoUnixTime
(
    FILETIME filetime
)
{   // 10000000 is the number of 100ns intervals in one second.
    // 11644473600 is the number of seconds between Jan 1 1601 00:00 and Jan 1 1970 00:00 UTC (the epoch difference.)
    ULARGE_INTEGER ll;
    ll.LowPart  = filetime.dwLowDateTime;
    ll.HighPart = filetime.dwHighDateTime;
    return ll.QuadPart / 10000000ULL - 11644473600ULL;
}

/// @summary Retrieve a system known folder path using SHGetKnownFolderPath.
/// @param buf The buffer to which the path will be copied.
/// @param buf_bytes The maximum number of bytes that can be written to buf.
/// @param buf_end On input, a pointer to the end of the destination buffer. On return, this value points to the zero terminator of the destination buffer.
/// @param bytes_needed On return, this location stores the number of bytes required to store the path string, including the zero terminator.
/// @param folder_id The Windows Shell known folder identifier.
/// @return The number of characters written to the destination buffer, not including the zero terminator.
public_function size_t
OsShellFolderPath
(
    WCHAR                 *buf, 
    size_t           buf_bytes, 
    WCHAR            **buf_end,
    size_t       &bytes_needed, 
    REFKNOWNFOLDERID folder_id
)
{
    HRESULT result = S_OK;
    WCHAR  *sysbuf = NULL;
    if (SUCCEEDED((result = SHGetKnownFolderPath(folder_id, KF_FLAG_NO_ALIAS, NULL, &sysbuf))))
    {   // calculate bytes needed, including the zero terminator.
        if ((bytes_needed = (wcslen(sysbuf) + 1) * sizeof(WCHAR)) > buf_bytes)
        {   // in the case of insufficient space, do not print an error. this is expected behavior.
            if (buf      != NULL) *buf = 0;
            if (buf_end  != NULL) *buf_end = buf;
            CoTaskMemFree(sysbuf);
            return 0;
        }
        if (buf != NULL)
        {   // copy the system-allocated string over to the destination buffer.
            // the system-allocated string is already zero-terminated.
            size_t chars_written  = bytes_needed / sizeof(WCHAR) - sizeof(WCHAR);
            CopyMemory(buf, sysbuf, bytes_needed);
            CoTaskMemFree(sysbuf);
            if (buf_end != NULL)
            {
               *buf_end = buf + chars_written;
            }
            return chars_written;
        }
        else
        {   // the caller didn't supply an input buffer.
            if (buf_end != NULL) *buf_end = NULL;
            CoTaskMemFree(sysbuf);
            return 0;
        }
    }
    else
    {   // unable to retrieve the specified path for some reason - the problem is NOT that the destination buffer is NULL.
        OsLayerError("ERROR: %S(%u): Unable to retrieve known folder path (HRESULT = %08X).\n", __FUNCTION__, GetCurrentThreadId(), result);
        if (buf     != NULL) *buf = 0;
        if (buf_end != NULL) *buf_end = buf;
        bytes_needed = 0;
        return 0;
    }
}

/// @summary Retrieve a special system folder path. 
/// @param buf The buffer to which the path will be copied.
/// @param buf_bytes The maximum number of bytes that can be written to buf.
/// @param buf_end On input, a pointer to the end of the destination buffer. On return, this value points to the zero terminator of the destination buffer.
/// @param bytes_needed On return, this location stores the number of bytes required to store the path string, including the zero terminator.
/// @param folder_id One of OS_KNOWN_PATH identifying the folder.
/// @return The number of characters written to the destination buffer, not including the zero terminator.
public_function size_t 
OsKnownPath
(
    WCHAR           *buf, 
    size_t     buf_bytes, 
    WCHAR      **buf_end,
    size_t &bytes_needed, 
    int        folder_id
)
{
    switch (folder_id)
    {
    case OS_KNOWN_PATH_EXECUTABLE:
        {   // allocate a temporary heap buffer for the executable path string.
            // do this on the heap because the returned string could be a long-format string with "\\?\" prefix.
            // these strings can be up to 32767 characters in length, plus one for the zero terminator.
            size_t buflen =  0; // in characters
            size_t bufsiz =  32768 *  sizeof(WCHAR);
            WCHAR *sysbuf = (WCHAR *) malloc(bufsiz);
            WCHAR *bufitr =  NULL;
            if (sysbuf == NULL)
            {
                OsLayerError("ERROR: %S(%u): Unable to allocate temporary memory for executable path string.\n", __FUNCTION__, GetCurrentThreadId());
                if (buf     != NULL) *buf = 0;
                if (buf_end != NULL) *buf_end = buf;
                bytes_needed = 0;
                return 0;
            }
            if ((buflen = GetModuleFileNameW(NULL, sysbuf, 32768)) == 0)
            {
                OsLayerError("ERROR: %S(%u): Unable to retrieve the executable path (%08X).\n", __FUNCTION__, GetCurrentThreadId(), GetLastError());
                if (buf     != NULL) *buf = 0;
                if (buf_end != NULL) *buf_end = buf;
                bytes_needed = 0;
                free(sysbuf);
                return 0;
            }
            // the path sitting in sysbuf contains the name of the executable. 
            // when the loop terminates, bufitr points at the zero-terminator.
            // replace the trailing slash with a zero-terminator.
            bytes_needed  =(buflen + 1) * sizeof(WCHAR);
            bufitr        = sysbuf + buflen;
            while (bufitr > sysbuf)
            {
                if (*bufitr == L'\\')
                {   // replace the trailing slash with a zero terminator.
                    *bufitr = 0;
                    break;
                }
                bytes_needed -= sizeof(WCHAR);
                bufitr--;
            }
            if (buf_bytes < bytes_needed)
            {   // don't print an error in this case, as this is expected usage.
                if (buf     != NULL) *buf = 0;
                if (buf_end != NULL) *buf_end = buf;
                free(sysbuf);
                return 0;
            }
            if (buf != NULL)
            {   // copy the path string to the destination buffer.
                size_t chars_written  = bytes_needed / sizeof(WCHAR) - sizeof(WCHAR);
                CopyMemory(buf, sysbuf, bytes_needed);
                free(sysbuf);
                if (buf_end != NULL)
                {   // chars_written does not include the zero-terminator, so +1 to include it.
                   *buf_end = buf + chars_written;
                }
                return chars_written;
            }
            else
            {   // the caller didn't supply an input buffer.
                if (buf_end != NULL) *buf_end = NULL;
                free(sysbuf);
                return 0;
            }
        }
    case OS_KNOWN_PATH_USER_HOME:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_Profile);
    case OS_KNOWN_PATH_USER_DESKTOP:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_Desktop);
    case OS_KNOWN_PATH_USER_DOCUMENTS:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_Documents);
    case OS_KNOWN_PATH_USER_DOWNLOADS:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_Downloads);
    case OS_KNOWN_PATH_USER_MUSIC:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_Music);
    case OS_KNOWN_PATH_USER_PICTURES:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_Pictures);
    case OS_KNOWN_PATH_USER_SAVE_GAMES:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_SavedGames);
    case OS_KNOWN_PATH_USER_VIDEOS:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_Videos);
    case OS_KNOWN_PATH_USER_PREFERENCES:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_LocalAppData);
    case OS_KNOWN_PATH_PUBLIC_DOCUMENTS:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_PublicDocuments);
    case OS_KNOWN_PATH_PUBLIC_DOWNLOADS:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_PublicDownloads);
    case OS_KNOWN_PATH_PUBLIC_MUSIC:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_PublicMusic);
    case OS_KNOWN_PATH_PUBLIC_PICTURES:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_PublicPictures);
    case OS_KNOWN_PATH_PUBLIC_VIDEOS:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_PublicVideos);
    case OS_KNOWN_PATH_SYSTEM_FONTS:
        return OsShellFolderPath(buf, buf_bytes, buf_end, bytes_needed, FOLDERID_Fonts);

    default:
        break;
    }
    bytes_needed = 0;
    return false;
}

/// @summary Retrieve a fully resolved and normalized path for a file or directory, given a file or directory object.
/// @param buf The destination buffer.
/// @param buf_bytes The maximum number of bytes that can be written to the destination buffer.
/// @param buf_end On input, a pointer to the end of the destination buffer. On return, this value points to the zero terminator of the destination buffer.
/// @param bytes_needed On return, this value specifies the number of bytes needed to store the path string, including the zero terminator.
/// @param handle The handle to the file or directory, or INVALID_HANDLE_VALUE.
/// @return The number of characters written to the destination buffer, not including the zero terminator.
public_function size_t
OsNativePathForHandle
(
    WCHAR           *buf, 
    size_t     buf_bytes, 
    WCHAR      **buf_end,
    size_t &bytes_needed, 
    HANDLE        handle
)
{
    size_t chars_written = 0;
    // GetFinalPathNameByHandle returns the required buffer length, in TCHARs, including zero-terminator.
    if ((bytes_needed = (GetFinalPathNameByHandleW(handle, NULL, 0, VOLUME_NAME_DOS | FILE_NAME_NORMALIZED) * sizeof(WCHAR))) == 0)
    {   // in this case, bytes_needed cannot be determined, so it is set to zero.
        OsLayerError("ERROR: %S(%u): Failed to retrieve absolute path for handle %p (%08X).\n", __FUNCTION__, GetCurrentThreadId(), handle, GetLastError());
        if (buf     != NULL) *buf = 0;
        if (buf_end != NULL) *buf_end = buf;
        bytes_needed = 0;
        return 0;
    }
    if (buf_bytes < bytes_needed || buf == NULL)
    {   // in the case of insufficient space, don't output any error message (this is an expected path.)
        // preserve the bytes_needed value for the caller.
        if (buf     != NULL) *buf = 0;
        if (buf_end != NULL) *buf_end = buf;
        return 0;
    }
    if ((chars_written = GetFinalPathNameByHandleW(handle, buf, (DWORD)(buf_bytes / sizeof(WCHAR) - sizeof(WCHAR)), VOLUME_NAME_DOS | FILE_NAME_NORMALIZED)) == 0)
    {   // preserve the bytes_needed value for the caller.
        OsLayerError("ERROR: %S(%u): Failed to retrieve absolute path for handle %p (%08X).\n", __FUNCTION__, GetCurrentThreadId(), handle, GetLastError());
        if (buf     != NULL) *buf = 0;
        if (buf_end != NULL) *buf_end = buf;
        return 0;
    }
    if (buf_end != NULL) 
    {   // chars_written does not include the zero terminator, so +1 to include it.
        *buf_end = buf + chars_written;
    }
    return chars_written;
}

/// @summary Append a directory fragment, filename, or directory and filename to an existing native path string.
/// @param buf The destination buffer.
/// @param buf_bytes The maximum number of bytes that can be written to the destination buffer.
/// @param buf_end On input, a pointer to the end of the destination buffer. On return, this value points to the zero terminator of the destination buffer.
/// @param bytes_needed On return, this value specifies the number of bytes needed to store the entire path string, including the zero terminator.
/// @param append A pointer to a zero-terminated string specifying the directory fragment, filename, or directory and filename to append.
/// @return The number of characters in the destination buffer, not including the zero terminator.
public_function size_t
OsNativePathAppend
(
    WCHAR           *buf, 
    size_t     buf_bytes, 
    WCHAR      **buf_end, 
    size_t &bytes_needed,
    WCHAR const  *append
)
{
    WCHAR const *app_end = NULL;
    WCHAR       *inp_end = NULL;
    size_t     inp_chars = 0; // the number of characters in string buf, not including zero terminator
    size_t     inp_bytes = 0; // the number of bytes in string buf, including zero terminator
    size_t     app_chars = 0; // the number of characters in string append, not including zero terminator
    size_t     app_bytes = 0; // the number of bytes in string append, including zero terminator
    size_t     sep_chars = 0; // 1 if a path separator is needed

    if (buf_end != NULL && *buf_end != NULL && *buf_end > buf)
    {   // *buf_end points to the zero-terminator of the existing string.
        inp_end    = *buf_end;
        inp_bytes  =((uint8_t*)*buf_end) - ((uint8_t*)buf);
        inp_chars  =(*buf_end - buf);
    }
    else if (buf != NULL && buf_bytes > sizeof(WCHAR))
    {   // determine the length of the input by scanning for the zero terminator.
        if (SUCCEEDED(StringCchLengthW(buf, buf_bytes, &inp_chars)))
        {
            if (buf_end != NULL) *buf_end = buf + inp_chars;
            inp_end   = buf + inp_chars;
            inp_bytes =(inp_chars + 1) * sizeof(WCHAR); 
        }
        else
        {   // unable to determine the length of the input string.
            OsLayerError("ERROR: %S(%u): Unable to determine length of input buffer. Is buf_bytes (%Iu) set correctly?\n", __FUNCTION__, GetCurrentThreadId(), buf_bytes);
            if (buf_end != NULL) *buf_end = buf;
            bytes_needed = 0;
            return 0;
        }
    }
    else
    {   // no input buffer was supplied, which is unusual.
        OsLayerError("%S(%u): No input buffer supplied.\n", __FUNCTION__, GetCurrentThreadId());
        if (buf_end != NULL) *buf_end = NULL;
        bytes_needed = 0;
        return 0;
    }
    if (append != NULL)
    {   // retieve the length of the string to append.
        if (SUCCEEDED(StringCchLengthW(append, STRSAFE_MAX_CCH, &app_chars)))
        {
            app_end   = append + app_chars;
            app_bytes =(app_chars + 1) * sizeof(WCHAR);
        }
        else
        {   // unable to determine the length of the fragment string.
            OsLayerError("ERROR: %S(%u): Unable to determine length of fragment buffer %p.\n", __FUNCTION__, GetCurrentThreadId(), append);
            if (buf_end != NULL) *buf_end = inp_end;
            bytes_needed = inp_bytes;
            return inp_chars;
        }
    }
    else
    {   // there's no string to append, so just return data for the existing buffer.
        if (buf_end != NULL) *buf_end = inp_end;
        bytes_needed = inp_bytes;
        return inp_chars;
    }
    if (*(inp_end - 1) != L'\\' && *(inp_end - 1) != L'/')
    {   // a trailing directory separator is required.
        sep_chars = 1;
    }
    else if (*(inp_end - 1) == L'/')
    {   // normalize the trailing separator to the OS-preferred format.
        *(inp_end - 1) = L'\\';
    }
    
    // we have enough information to determine the number of bytes required in the destination buffer.
    bytes_needed  =(inp_chars + sep_chars + app_chars + 1) * sizeof(WCHAR);
    if (buf_bytes < bytes_needed)
    {   // insufficient space in the input buffer. this is an expected occurrence.
        if (buf_end != NULL) *buf_end = inp_end;
        return inp_chars;
    }
    if (sep_chars > 0)
    {   // append a directory separator character.
        *inp_end++ = L'\\';
    }
    // append the fragment to the native path string.
    while (append < app_end)
    {
        if (*append != L'/')
        {   // append the character to the destination buffer as-is.
            *inp_end++ = *append++;
        }
        else
        {   // convert to the preferred native path separator character.
            *inp_end++ = L'\\';
            ++append;
        }
    }
    // zero-terminate the string.
    *inp_end = 0;
    // all finished; set output parameters.
    if (buf_end != NULL) *buf_end = inp_end;
    return (inp_chars + sep_chars + app_chars);
}

/// @summary Change (or remove) the file extension of a native platform path string.
/// @param buf The destination buffer.
/// @param buf_bytes The maximum number of bytes that can be written to the destination buffer.
/// @param buf_end On input, a pointer to the end of the destination buffer. On return, this value points to the zero terminator of the destination buffer.
/// @param bytes_needed On return, this value specifies the number of bytes needed to store the entire path string, including the zero terminator.
/// @param new_ext A pointer to a zero-terminated string specifying the new file extension, or NULL (or an empty string) to remove the existing extension.
/// @return The number of characters in the destination buffer, not including the zero terminator.
public_function size_t
OsNativePathChangeExtension
(
    WCHAR           *buf, 
    size_t     buf_bytes, 
    WCHAR      **buf_end, 
    size_t &bytes_needed, 
    WCHAR const *new_ext
)
{
    WCHAR const *ext_end = NULL;
    WCHAR       *inp_end = NULL;
    WCHAR       *inp_ext = NULL;
    WCHAR       *inp_itr = NULL;
    size_t     inp_chars = 0; // the number of characters in string buf, not including zero terminator
    size_t     inp_bytes = 0; // the number of bytes in string buf, including zero terminator
    size_t     ext_chars = 0; // the number of characters in string append, not including zero terminator
    size_t     ext_bytes = 0; // the number of bytes in string append, including zero terminator
    size_t     sep_chars = 0; // 1 if an extension separator is needed

    if (buf_end != NULL && *buf_end != NULL && *buf_end > buf)
    {   // *buf_end points to the zero-terminator of the existing string.
        inp_end    = *buf_end;
        inp_bytes  =((uint8_t*)*buf_end) - ((uint8_t*)buf);
        inp_chars  =(*buf_end - buf);
    }
    else if (buf != NULL && buf_bytes > sizeof(WCHAR))
    {   // determine the length of the input by scanning for the zero terminator.
        if (SUCCEEDED(StringCchLengthW(buf, buf_bytes, &inp_chars)))
        {
            if (buf_end != NULL) *buf_end = buf + inp_chars;
            inp_end   = buf + inp_chars;
            inp_bytes =(inp_chars + 1) * sizeof(WCHAR); 
        }
        else
        {   // unable to determine the length of the input string.
            OsLayerError("ERROR: %S(%u): Unable to determine length of input buffer. Is buf_bytes (%Iu) set correctly?\n", __FUNCTION__, GetCurrentThreadId(), buf_bytes);
            if (buf_end != NULL) *buf_end = buf;
            bytes_needed = 0;
            return 0;
        }
    }
    else
    {   // no input buffer was supplied, which is unusual.
        OsLayerError("%S(%u): No input buffer supplied.\n", __FUNCTION__, GetCurrentThreadId());
        if (buf_end != NULL) *buf_end = NULL;
        bytes_needed = 0;
        return 0;
    }

    // find the first period after the last path separator.
    inp_itr = inp_end - 1;
    while (inp_itr > buf)
    {
        if (*inp_itr == L'.')
        {   // save the position of the extension separator.
            inp_ext = inp_itr;
        }
        else if (*inp_itr == L'\\' || *inp_itr == L'/')
        {   // found a path separator; terminate the search.
            break;
        }
        inp_itr--;
    }
    if (inp_ext == NULL)
    {   // the input string currently has no extension, so append one.
        inp_ext = inp_end;
    }

    if (new_ext == NULL || *new_ext == 0)
    {   // the current extension is being removed; truncate the existing string.
        if (buf_end != NULL) *buf_end = inp_ext;
        bytes_needed = ((uint8_t*)inp_ext) - ((uint8_t*)buf);
        *inp_ext = 0;
        return (inp_ext - buf);
    }
    else
    {   // a new extension is being appended; retrieve its length.
        if (SUCCEEDED(StringCchLengthW(new_ext, STRSAFE_MAX_CCH, &ext_chars)))
        {
            ext_end   = new_ext + ext_chars;
            ext_bytes =(ext_chars + 1) * sizeof(WCHAR);
        }
        else
        {   // unable to determine the length of the new extension string.
            OsLayerError("ERROR: %S(%u): Unable to determine length of extension buffer %p.\n", __FUNCTION__, GetCurrentThreadId(), new_ext);
            if (buf_end != NULL) *buf_end = inp_end;
            bytes_needed = inp_bytes;
            return inp_chars;
        }
    }
    if (*new_ext != L'.')
    {   // a leading extension separator is required.
        sep_chars = 1;
    }

    // we have enough information to determine the number of bytes required in the destination buffer.
    bytes_needed  =(inp_chars - (inp_end - inp_ext) + sep_chars + ext_chars + 1) * sizeof(WCHAR);
    if (buf_bytes < bytes_needed)
    {   // insufficient space in the input buffer. this is an expected occurrence.
        if (buf_end != NULL) *buf_end = inp_end;
        return inp_chars;
    }
    if (sep_chars > 0)
    {   // append an extension separator character.
        *inp_ext++ = L'.';
    }
    // append the extension to the native path string, including the zero-terminator.
    CopyMemory(inp_ext, new_ext, ext_bytes);
    inp_ext += ext_chars;
    // all finished; set output parameters.
    if (buf_end != NULL) *buf_end = inp_ext;
    return ((inp_ext - buf) + sep_chars + ext_chars);
}

/// @summary Append an extension value to a native path string.
/// @param buf The destination buffer.
/// @param buf_bytes The maximum number of bytes that can be written to the destination buffer.
/// @param buf_end On input, a pointer to the end of the destination buffer. On return, this value points to the zero terminator of the destination buffer.
/// @param bytes_needed On return, this value specifies the number of bytes needed to store the entire path string, including the zero terminator.
/// @param new_ext A pointer to a zero-terminated string specifying the file extension to append.
/// @return The number of characters in the destination buffer, not including the zero terminator.
public_function size_t
OsNativePathAppendExtension
(
    WCHAR           *buf, 
    size_t     buf_bytes, 
    WCHAR      **buf_end, 
    size_t &bytes_needed, 
    WCHAR const *new_ext
)
{
    WCHAR const *ext_end = NULL;
    WCHAR       *inp_end = NULL;
    size_t     inp_chars = 0; // the number of characters in string buf, not including zero terminator
    size_t     inp_bytes = 0; // the number of bytes in string buf, including zero terminator
    size_t     ext_chars = 0; // the number of characters in string append, not including zero terminator
    size_t     ext_bytes = 0; // the number of bytes in string append, including zero terminator
    size_t     sep_chars = 0; // 1 if an extension separator is needed

    if (buf_end != NULL && *buf_end != NULL && *buf_end > buf)
    {   // *buf_end points to the zero-terminator of the existing string.
        inp_end    = *buf_end;
        inp_bytes  =((uint8_t*)*buf_end) - ((uint8_t*)buf);
        inp_chars  =(*buf_end - buf);
    }
    else if (buf != NULL && buf_bytes > sizeof(WCHAR))
    {   // determine the length of the input by scanning for the zero terminator.
        if (SUCCEEDED(StringCchLengthW(buf, buf_bytes, &inp_chars)))
        {
            if (buf_end != NULL) *buf_end = buf + inp_chars;
            inp_end   = buf + inp_chars;
            inp_bytes =(inp_chars + 1) * sizeof(WCHAR); 
        }
        else
        {   // unable to determine the length of the input string.
            OsLayerError("ERROR: %S(%u): Unable to determine length of input buffer. Is buf_bytes (%Iu) set correctly?\n", __FUNCTION__, GetCurrentThreadId(), buf_bytes);
            if (buf_end != NULL) *buf_end = buf;
            bytes_needed = 0;
            return 0;
        }
    }
    else
    {   // no input buffer was supplied, which is unusual.
        OsLayerError("%S(%u): No input buffer supplied.\n", __FUNCTION__, GetCurrentThreadId());
        if (buf_end != NULL) *buf_end = NULL;
        bytes_needed = 0;
        return 0;
    }

    if (new_ext == NULL || *new_ext == 0)
    {   // no extension was supplied; don't make any changes.
        if (buf_end != NULL) *buf_end = inp_end;
        bytes_needed = inp_bytes;
        return inp_chars;
    }
    else
    {   // a new extension is being appended; retrieve its length.
        if (SUCCEEDED(StringCchLengthW(new_ext, STRSAFE_MAX_CCH, &ext_chars)))
        {
            ext_end   = new_ext + ext_chars;
            ext_bytes =(ext_chars + 1) * sizeof(WCHAR);
        }
        else
        {   // unable to determine the length of the new extension string.
            OsLayerError("ERROR: %S(%u): Unable to determine length of extension buffer %p.\n", __FUNCTION__, GetCurrentThreadId(), new_ext);
            if (buf_end != NULL) *buf_end = inp_end;
            bytes_needed = inp_bytes;
            return inp_chars;
        }
    }
    if (*new_ext != L'.')
    {   // a leading extension separator is required.
        sep_chars = 1;
    }

    // we have enough information to determine the number of bytes required in the destination buffer.
    bytes_needed  =(inp_chars + sep_chars + ext_chars + 1) * sizeof(WCHAR);
    if (buf_bytes < bytes_needed)
    {   // insufficient space in the input buffer. this is an expected occurrence.
        if (buf_end != NULL) *buf_end = inp_end;
        return inp_chars;
    }
    if (sep_chars > 0)
    {   // append an extension separator character.
        *inp_end++ = L'.';
    }
    // append the extension to the native path string, including the zero-terminator.
    CopyMemory(inp_end, new_ext, ext_bytes);
    inp_end += ext_chars;
    // all finished; set output parameters.
    if (buf_end != NULL) *buf_end = inp_end;
    return (inp_chars + sep_chars + ext_chars);
}

/// @summary Parse an absolute or relative native path string into its constituent parts.
/// @param buf The buffer to parse containing the native path string. All forward slashes are replaced with backslash.
/// @param buf_end A pointer to the zero-terminator byte of the input path string, or NULL to scan the input buffer for a zero terminator.
/// @param parts The OS_PATH_PARTS to populate with path information.
/// @return Zero if the path string is successfully parsed, or -1 if an error occurred.
public_function int
OsNativePathParse
(
    WCHAR           *buf, 
    WCHAR       *buf_end, 
    OS_PATH_PARTS *parts
)
{
    size_t inp_chars = 0; // the number of characters in string buf, not including zero terminator
    size_t inp_bytes = 0; // the number of bytes in string buf, including zero terminator

    // initialize the output to valid zero-length strings.
    parts->Root      = buf;     parts->RootEnd      = buf_end;
    parts->Path      = buf_end; parts->PathEnd      = buf_end;
    parts->Filename  = buf_end; parts->FilenameEnd  = buf_end;
    parts->Extension = buf_end; parts->ExtensionEnd = buf_end;

    // figure out the length of the input path string.
    if (buf_end != NULL && buf_end > buf)
    {   // buf_end points to the zero-terminator of the existing string.
        inp_bytes = ((uint8_t*)buf_end) - ((uint8_t*)buf);
        inp_chars = (buf_end - 1 - buf);
    }
    else if (buf != NULL)
    {   // determine the length of the input by scanning for the zero terminator.
        StringCchLengthW(buf, STRSAFE_MAX_CCH, &inp_chars);
        buf_end   = buf + inp_chars;
        inp_bytes = (inp_chars + 1) * sizeof(WCHAR); 
    }
    if (buf == NULL || inp_chars < 1)
    {   // the input buffer is invalid; there's nothing to parse.
        parts->PathFlags = OS_PATH_FLAGS_INVALID;
        return -1;
    }

    // determine the type of input path string and perform setup for OsExtractNativePathParts.
    if (inp_chars >= 3)
    {
        if (buf[0] == L'\\' && buf[1] == L'\\')
        {   // absolute path; may be device, UNC, long device, long UNC, or long DOS.
            if ((inp_chars >= 5) && (buf[2] == L'?') && (buf[3] == L'\\'))
            {   // may be long UNC or long DOS.
                if ((inp_chars >= 6) && ((buf[4] >= L'A' && buf[4] <= L'Z') || (buf[4] >= L'a' && buf[4] <= L'z')) && (buf[5] == L':'))
                {   // long DOS path.
                    parts->Root      = buf + 4;
                    parts->RootEnd   = buf + 6;
                    parts->PathFlags = OS_PATH_FLAG_ABSOLUTE | OS_PATH_FLAG_LONG | OS_PATH_FLAG_ROOT;
                    return OsExtractNativePathParts(buf, buf_end, parts);
                }
                else if ((inp_chars >= 6) && (buf[4] == L'.' && buf[5] == L'\\'))
                {   // long device path.
                    parts->Root      = buf + 6;
                    parts->RootEnd   = buf + 6;
                    parts->PathFlags = OS_PATH_FLAG_ABSOLUTE | OS_PATH_FLAG_LONG | OS_PATH_FLAG_DEVICE | OS_PATH_FLAG_ROOT;
                    goto scan_for_end_of_root;
                }
                else
                {   // long UNC path.
                    parts->Root      = buf + 4;
                    parts->RootEnd   = buf + 4; 
                    parts->PathFlags = OS_PATH_FLAG_ABSOLUTE | OS_PATH_FLAG_LONG | OS_PATH_FLAG_UNC | OS_PATH_FLAG_ROOT;
                    goto scan_for_end_of_root;
                }
            }
            else if ((inp_chars >= 5) && (buf[2] == L'.') && (buf[3] == L'\\'))
            {   // device path, limit MAX_PATH characters.
                parts->Root      = buf + 4;
                parts->RootEnd   = buf + 4;
                parts->PathFlags = OS_PATH_FLAG_ABSOLUTE | OS_PATH_FLAG_DEVICE | OS_PATH_FLAG_ROOT;
                goto scan_for_end_of_root;
            }
            else
            {   // UNC path, limit MAX_PATH characters.
                parts->Root      = buf + 2;
                parts->RootEnd   = buf + 2;
                parts->PathFlags = OS_PATH_FLAG_ABSOLUTE | OS_PATH_FLAG_UNC | OS_PATH_FLAG_ROOT;
                goto scan_for_end_of_root;
            }
        }
        else if (buf[0] == L'\\' || buf[0] == L'/')
        {   // absolute path, with a root of '\' (MSDN says this is valid?)
            if (buf[0] == L'/')
                buf[0] = L'\\';
            parts->Root      = buf;
            parts->RootEnd   = buf + 1;
            parts->PathFlags = OS_PATH_FLAG_ABSOLUTE | OS_PATH_FLAG_ROOT;
            return OsExtractNativePathParts(buf, buf_end, parts);
        }
        else if (((buf[0] >= L'A' && buf[0] <= L'Z') || (buf[0] >= L'a' && buf[0] <= L'z')) && (buf[1] == L':'))
        {   // absolute DOS path with a drive letter root.
            parts->Root      = buf;
            parts->RootEnd   = buf + 2;
            parts->PathFlags = OS_PATH_FLAG_ABSOLUTE | OS_PATH_FLAG_ROOT;
            return OsExtractNativePathParts(buf, buf_end, parts);
        }
        else
        {   // assume that this is a relative path.
            parts->Root      = buf;
            parts->RootEnd   = buf;
            parts->PathFlags = OS_PATH_FLAG_RELATIVE;
            return OsExtractNativePathParts(buf, buf_end, parts);
        }
    }
    else if (inp_chars == 2)
    {   // C:, .., .\, aa, .a, etc.
        if (((buf[0] >= L'A' && buf[0] <= L'Z') || (buf[0] >= L'a' && buf[0] <= L'z')) && (buf[1] == L':'))
        {   // absolute DOS path with drive letter root; no path information.
            parts->Root      = buf;
            parts->RootEnd   = buf + 2;
            parts->PathFlags = OS_PATH_FLAG_ABSOLUTE | OS_PATH_FLAG_ROOT;
            return 0;
        }
        // else, assume relative path, directory path info only.
        parts->Root      = buf;
        parts->RootEnd   = buf;
        parts->Path      = buf;
        parts->PathFlags = OS_PATH_FLAG_RELATIVE | OS_PATH_FLAG_PATH;
        if (buf[0] == L'.' && (buf[1] == L'\\' || buf[1] == L'/'))
        {   // relative path, directory path info only.
            if (buf[1] == L'/')
                buf[1] = L'\\';
            parts->PathEnd = buf + 1;
        }
        else
        {   // assume this is a relative directory path.
            parts->PathEnd = buf + 2;
        }
        return 0;
    }
    else
    {   // /, ., a, etc.
        if (buf[0] == L'/')
            buf[0] = L'\\';
        if (buf[0] == L'\\')
        {   // treat this as an absolute path, the root of the filesystem.
            parts->Root      = buf;
            parts->RootEnd   = buf;
            parts->Path      = buf;
            parts->PathEnd   = buf + 1;
            parts->PathFlags = OS_PATH_FLAG_ABSOLUTE | OS_PATH_FLAG_PATH;
        }
        else
        {   // assume this is a relative path, directory info only.
            parts->Root      = buf;
            parts->RootEnd   = buf;
            parts->Path      = buf;
            parts->PathEnd   = buf + 1;
            parts->PathFlags = OS_PATH_FLAG_RELATIVE | OS_PATH_FLAG_PATH;
        }
        return 0;
    }

scan_for_end_of_root:
    while (parts->RootEnd < buf_end)
    {
        if (parts->RootEnd[0] == L'\\')
            break;
        if (parts->RootEnd[0] == L'/')
        {
            parts->RootEnd[0] = L'\\';
            break;
        }
        parts->RootEnd++;
    }
    if (parts->RootEnd == buf_end)
    {   // no additional components will be found.
        return 0;
    }
    return OsExtractNativePathParts(buf, buf_end, parts);
}

/// @summary Retrieve the physical sector size for a block-access device.
/// @param device A handle to the device to query. This handle must be obtained by opening a device path, not a file.
/// @return The size of a physical sector on the specified device.
public_function size_t 
OsPhysicalSectorSize
(
    HANDLE device
)
{   // http://msdn.microsoft.com/en-us/library/ff800831(v=vs.85).aspx
    // for structure STORAGE_ACCESS_ALIGNMENT
    // Vista and Server 2008+ only - XP not supported.
    size_t const DefaultPhysicalSectorSize = 4096;
    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR desc;
    STORAGE_PROPERTY_QUERY    query;
    memset(&desc  , 0, sizeof(desc));
    memset(&query , 0, sizeof(query));
    query.QueryType  = PropertyStandardQuery;
    query.PropertyId = StorageAccessAlignmentProperty;
    DWORD bytes = 0;
    BOOL result = DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &desc , sizeof(desc), &bytes, NULL);
    return result ? desc.BytesPerPhysicalSector : DefaultPhysicalSectorSize;
}

/// @summary Initialize a file system information chunk allocator.
/// @param alloc The OS_FSIC_ALLOCATOR instance to initialize.
/// @param arena The memory arena from which chunks will be allocated. This arena must exist for the lifetime of the allocator.
public_function void
OsInitFileSystemInfoChunkAllocator
(
    OS_FSIC_ALLOCATOR *alloc, 
    OS_MEMORY_ARENA   *arena
)
{
    InitializeCriticalSectionAndSpinCount(&alloc->AllocatorLock, 0x1000);
    alloc->MemoryArena = arena;
    alloc->FreeList = NULL;
}

/// @summary Allocate a single OS_FILE_INFO_CHUNK. This function blocks the calling thread.
/// @param alloc The OS_FSIC_ALLOCATOR from which the chunk will be allocated.
/// @return A pointer to the OS_FILE_INFO_CHUNK, or NULL if the allocation failed.
public_function OS_FILE_INFO_CHUNK*
OsNewFileInfoChunk
(
    OS_FSIC_ALLOCATOR *alloc
)
{
    OS_FILE_INFO_CHUNK  *chunk = NULL;
    EnterCriticalSection(&alloc->AllocatorLock);
    {
        if (alloc->FreeList != NULL)
        {   // return a block from the free list.
            chunk            = alloc->FreeList;
            alloc->FreeList  = alloc->FreeList->NextChunk;
        }
        else
        {   // allocate a new block from the memory arena.
            chunk = OsMemoryArenaAllocate<OS_FILE_INFO_CHUNK>(alloc->MemoryArena);
        }
    }
    LeaveCriticalSection(&alloc->AllocatorLock);

    // initialize the chunk outside of the critical section.
    if (chunk != NULL)
    {
        InitializeSRWLock(&chunk->RWLock);
        chunk->NextChunk   = NULL;
        chunk->RecordCount = 0;
    }
    return chunk;
}

/// @summary Free a list of OS_FILE_INFO_CHUNK objects. This function blocks the calling thread.
/// @param alloc The OS_FSIC_ALLOCATOR to which the chunk will be returned.
/// @param chunk The OS_FILE_INFO_CHUNK defining the head of the list to free.
public_function void
OsFreeFileInfoChunkList
(
    OS_FSIC_ALLOCATOR  *alloc, 
    OS_FILE_INFO_CHUNK *chunk
)
{
    EnterCriticalSection(&alloc->AllocatorLock);
    {
        while (chunk != NULL)
        {
            OS_FILE_INFO_CHUNK *next = chunk->NextChunk;
            chunk->NextChunk = alloc->FreeList;
            alloc->FreeList  = chunk;
            chunk            = next;
        }
    }
    LeaveCriticalSection(&alloc->AllocatorLock);
}

/// @summary Acquire a read lock on a file metadata chunk to guard against concurrent modification. Multiple threads may read the chunk concurrently.
/// @param chunk The OS_FILE_INFO_CHUNK to lock for read-only access.
public_function void
OsLockFileInfoChunkRead
(
    OS_FILE_INFO_CHUNK *chunk
)
{
    AcquireSRWLockShared(&chunk->RWLock);
}

/// @summary Release a previously acquired read lock on a file metadata chunk.
/// @param chunk The OS_FILE_INFO_CHUNK locked for read-only access.
public_function void
OsUnlockFileInfoChunkRead
(
    OS_FILE_INFO_CHUNK *chunk
)
{
    ReleaseSRWLockShared(&chunk->RWLock);
}

/// @summary Acquire a read-write lock on a file metadata chunk to guard against concurrent modification. Only one thread may read or write the chunk.
/// @param chunk The OS_FILE_INFO_CHUNK to lock for read-write access.
public_function void
OsLockFileInfoChunkWrite
(
    OS_FILE_INFO_CHUNK *chunk
)
{
    AcquireSRWLockExclusive(&chunk->RWLock);
}

/// @summary Release a previously acquired read-write lock on a file metadata chunk.
/// @param chunk The OS_FILE_INFO_CHUNK locked for read-write access.
public_function void
OsUnlockFileInfoChunkWrite
(
    OS_FILE_INFO_CHUNK *chunk
)
{
    ReleaseSRWLockExclusive(&chunk->RWLock);
}

/// @summary Open a directory for access on the native file system. The directory may be local or remote. The directory must exist.
/// @param path A zero-terminated native path string specifying the directory to open.
/// @param dir On return, this location is updated with the native handle used to access the directory.
/// @return true if the directory is successfully opened.
public_function int
OsOpenNativeDirectory
(
    WCHAR const *path, 
    HANDLE       &dir
)
{
    if ((dir = CreateFile(path, 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL)) == INVALID_HANDLE_VALUE)
    {
        OsLayerError("ERROR: %S(%u): Unable to open directory %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
        return -1;
    }
    return 0;
}

/// @summary Close a native directory handle, indicating that the application no longer requires access.
/// @param dir The handle returned by OsOpenNativeDirectory.
public_function void
OsCloseNativeDirectory
(
    HANDLE dir
)
{
    if (dir != INVALID_HANDLE_VALUE)
        CloseHandle(dir);
}

/// @summary Search a native filesystem directory (and possibly its subdirectories) for files matching a specific criteria.
/// @param dir The handle of the root directory to search, as returned by OsOpenNativeDirectory.
/// @param filter A zero-terminated string specifying the filter to use when locating files. The filter supports wildcards. Specify a filter of * to include all files in the search.
/// @param recurse Specify true to include files located in subdirectories in the search results.
/// @param total_files This value is updated to include the total number of files returned by the search.
/// @param alloc The OS_FILE_INFO_CHUNK allocator to use for allocating blocks of file information to store the search results.
/// @return A pointer to the OS_FILE_INFO_CHUNK containing the search results, or NULL if no results were returned or an error occurred.
public_function OS_FILE_INFO_CHUNK*
OsNativeDirectoryFindFiles
(
    HANDLE               dir, 
    WCHAR const      *filter, 
    bool             recurse,
    size_t      &total_files, 
    OS_FSIC_ALLOCATOR *alloc
)
{
    OS_FILE_INFO_CHUNK *root = NULL;
    WCHAR           *pathbuf = NULL;
    WCHAR           *pathend = NULL;
    size_t            nbytes = 32768 * sizeof(WCHAR);
    size_t            nchars = 0;
    size_t           nneeded = 0;

    // reset the total files counter; no results have been returned yet.
    total_files = 0;
    // use the default filter (return all files) if none is specified.
    if (filter == NULL || filter[0] == 0)
        filter = L"*";
    // ensure that the directory handle is valid.
    if (dir == INVALID_HANDLE_VALUE)
    {
        OsLayerError("ERROR: %S(%u): Invalid directory handle.\n", __FUNCTION__, GetCurrentThreadId());
        total_files = 0;
        return NULL;
    }
    // allocate a temporary path buffer of the maximum allowable size.
    if ((pathbuf = (WCHAR*) malloc(nbytes)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate temporary path buffer.\n", __FUNCTION__, GetCurrentThreadId());
        total_files = 0;
        return NULL;
    }
    // retrieve the long-form, fully-resolved absolute path of the directory.
    if ((nchars = OsNativePathForHandle(pathbuf, nbytes, &pathend, nneeded, dir)) == 0)
    {   
        OsLayerError("ERROR: %S(%u): Unable to resolve path for input directory.\n", __FUNCTION__, GetCurrentThreadId());
        total_files = 0;
        free(pathbuf);
        return NULL;
    }
    // perform the actual enumeration. this loads metadata from the disk and may take some time.
    if ((root = OsEnumerateDirectory(pathbuf, nbytes, pathend, filter, recurse, total_files, alloc, NULL)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to enumerate files at %s with filter %s.\n", __FUNCTION__, GetCurrentThreadId(), pathbuf, filter);
        total_files = 0;
        free(pathbuf);
        return NULL;
    }
    // clean up and return.
    free(pathbuf);
    return root;
}

/// @summary Ensure that all directories in a path exist. Any directories that do not exist are created.
/// @param path A zero-terminated string specifying an absolute native filesystem path. All forward slashes are replaced with backslash.
/// @param path_end A pointer to the zero-terminator codepoint on the path string, or NULL to scan the input for the zero terminator.
/// @return Zero if all directories in the path exist or were created, or -1 if an error occurred.
public_function int
OsCreateNativeDirectory
(
    WCHAR     *path,
    WCHAR *path_end
)
{
    WCHAR      *pathbuf = NULL;
    WCHAR      *pathend = NULL;
    WCHAR       *dirend = NULL;
    size_t       nbytes = 32768 * sizeof(WCHAR);
    OS_PATH_PARTS parts = {};

    // parse the input path into its constituient parts.
    if (OsNativePathParse(path, path_end, &parts) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to parse input path \"%s\".\n", __FUNCTION__, GetCurrentThreadId(), path);
        return -1;
    }
    // ensure that an absolute path was supplied.
    if (parts.PathFlags & OS_PATH_FLAG_RELATIVE)
    {
        OsLayerError("ERROR: %S(%u): An absolute path is required. Path \"%s\" specifies a relative path.\n", __FUNCTION__, GetCurrentThreadId(), path);
        return -1;
    }
    // allocate a temporary path buffer of the maximum allowable size.
    if ((pathbuf = (WCHAR*) malloc(nbytes)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate temporary path buffer.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }
    // copy the root into the new path buffer.
    CopyMemory(pathbuf, path, (parts.RootEnd - path) * sizeof(WCHAR));
    pathend  = pathbuf      + (parts.RootEnd - path);
    *pathend++ = L'\\';
    *pathend = 0;
    // build up the path one directory at a time.
    // attempt to create each directory in the tree.
    dirend = parts.Path;
    do
    {   // search for the next directory separator character.
        // note that the path was normalized during parsing.
        while (dirend < parts.PathEnd)
        {
            if (*dirend == L'\\')
            {   // skip the path separator.
                dirend++;
                break;
            }
            *pathend++ = *dirend++;
        }
        *pathend = 0;
        // attempt to create the directory.
        if (!CreateDirectory(pathbuf, NULL))
        {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS)
            {
                OsLayerError("ERROR: %S(%u): Failed to create directory \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), pathbuf, error);
                free(pathbuf);
                return -1;
            }
        }
        // append a directory separator character.
        *pathend++ = L'\\';
    } while (dirend < parts.PathEnd);

    // clean up and return.
    free(pathbuf);
    return 0;
}

/// @summary Calculate the number of bytes in a string.
/// @param beg A pointer to the first character of the string.
/// @param end This value either points to the end of the string/zero terminator, or points to NULL, in which case the input string is scanned for a zero terminator and the location is written to this address.
/// @return The number of bytes in the string, including the zero terminator.
public_function size_t
OsStringByteCount
(
    char const  *beg, 
    char const **end
)
{   // length is the length in characters.
    size_t length = 0;
    if (beg != NULL)
    {   // typical case; the input string is not NULL.
        if ((end != NULL) && (*end > beg))
        {   // fast path - caller already knows where the end of the string is.
            length = (size_t)(*end - beg);
        }
        else
        {   // slow(er) path - scan for a zero terminator character.
            length = strlen(beg);
            if (end != NULL)
            {   // set the end-of-string pointer for the caller.
                *end = beg + length;
            }
            length++; // +1 to include zero-terminator character.
        }
    }
    return length;
}

/// @summary Calculate the number of bytes in a string.
/// @param beg A pointer to the first codepoint of the string.
/// @param end This value either points to the end of the string/zero terminator, or points to NULL, in which case the input string is scanned for a zero terminator and the location is written to this address.
/// @return The number of bytes in the string, including the zero terminator.
public_function size_t
OsStringByteCount
(
    WCHAR const  *beg, 
    WCHAR const **end
)
{   // length is the length in characters.
    size_t length = 0;
    if (beg != NULL)
    {   // typical case; the input string is not NULL.
        if ((end != NULL) && (*end > beg))
        {   // fast path - caller already knows where the end of the string is.
            length = (size_t)(*end - beg);
        }
        else
        {   // slow(er) path - scan for a zero terminator character.
            length = wcslen(beg);
            if (end != NULL)
            {   // set the end-of-string pointer for the caller.
                *end = beg + length;
            }
            length++; // +1 to include zero-terminator character.
        }
    }
    return length * sizeof(WCHAR);
}

/// @summary Load the entire contents of a file into memory.
/// @param data The OS_FILE_DATA instance to populate.
/// @param path The zero-terminated UTF-16 path of the file to load.
/// @return Zero if the file is loaded successfully, or -1 if an error occurred.
public_function int
OsLoadFileData
(
    OS_FILE_DATA *data,
    WCHAR const  *path
)
{
    LARGE_INTEGER file_size = {};
    HANDLE     fd = INVALID_HANDLE_VALUE;
    void     *buf = NULL;
    size_t     nb = 0;
    int64_t    nr = 0;

    // initialize the fields of the OS_FILE_DATA structure.
    ZeroMemory(data, sizeof(OS_FILE_DATA));

    // open the requested input file, read-only, to be read from start to end.
    if ((fd = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL)) == INVALID_HANDLE_VALUE)
    {
        OsLayerError("ERROR: %S(%u): Unable to open input file \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
        goto cleanup_and_fail;
    }
    // retrieve the file size, and use that to allocate a buffer for the file data.
    if (!GetFileSizeEx(fd, &file_size))
    {
        OsLayerError("ERROR: %S(%u): Failed to retrieve file size for input file \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
        goto cleanup_and_fail;
    }
    if ((nb = (size_t) file_size.QuadPart) == 0 || (buf = malloc(nb)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Failed to allocate %Iu byte input buffer for file \"%s\".\n", __FUNCTION__, GetCurrentThreadId(), nb, path);
        goto cleanup_and_fail;
    }
    // read the entire file contents into the buffer, in 1MB chunks.
    while (nr < file_size.QuadPart)
    {
        uint8_t     *dst =(uint8_t*) buf + nr;
        int64_t   remain = file_size.QuadPart - nr;
        DWORD    to_read =(remain < Megabytes(1)) ? (DWORD) remain : (DWORD) Megabytes(1); 
        DWORD bytes_read = 0;
        if (!ReadFile(fd, dst, to_read, &bytes_read, NULL))
        {   // the read failed. treat this as a fatal error unless it's EOF.
            if (GetLastError() != ERROR_HANDLE_EOF)
            {
                OsLayerError("ERROR: %S(%u): ReadFile failed for input file \"%s\", offset %I64d (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, nr, GetLastError());
                goto cleanup_and_fail;
            }
            else
            {   // reached end-of-file.
                nr += bytes_read;
                break;
            }
        }
        else
        {   // the read completed successfully.
            nr += bytes_read;
        }
    }
    // the file was successfully read, so clean up and set the fields on the FILE_DATA.
    CloseHandle(fd);
    data->Buffer   =(uint8_t*) buf;
    data->MapPtr   = NULL;
    data->Offset   = 0;
    data->DataSize = file_size.QuadPart;
    data->Flags    = OS_FILE_DATA_FLAG_COMMITTED;
    return 0;

cleanup_and_fail:
    ZeroMemory(data, sizeof(OS_FILE_DATA));
    if (fd != INVALID_HANDLE_VALUE) CloseHandle(fd);
    if (buf != NULL) free(buf);
    return -1;
}

/// @summary Open a file for memory-mapped I/O optimized for sequential reads.
/// @param file The OS_FILE_MAPPING object to initialize.
/// @param path The zero-terminated UTF-16 path of the file to open.
/// @return Zero if the file mapping was opened successfully, or -1 if an error occurred.
public_function int
OsOpenFileMapping
(
    OS_FILE_MAPPING *file, 
    WCHAR const     *path
)
{
    SYSTEM_INFO    sys_info = {};
    LARGE_INTEGER file_size = {};
    HANDLE     fd = INVALID_HANDLE_VALUE;
    HANDLE    map = NULL;

    // initialize the fields of the OS_FILE_MAPPING structure.
    ZeroMemory(file, sizeof(OS_FILE_MAPPING));

    // open the requested input file, read-only, to be read from start to end.
    if ((fd = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL)) == INVALID_HANDLE_VALUE)
    {
        OsLayerError("ERROR: %S(%u): Unable to open input file \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
        goto cleanup_and_fail;
    }
    // retrieve the current size of the file, in bytes.
    if (!GetFileSizeEx(fd, &file_size))
    {
        OsLayerError("ERROR: %S(%u): Failed to retrieve file size for input file \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
        goto cleanup_and_fail;
    }
    // map the entire file (but don't map a view of the file.)
    if ((map = CreateFileMapping(fd, NULL, PAGE_READONLY, 0, 0, NULL)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Failed to create the file mapping for input file \"%s\" (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
        goto cleanup_and_fail;
    }
    // retrieve system information to get the allocation granularity.
    GetNativeSystemInfo(&sys_info);

    // all finished. the user should call IoMapFileRegion next.
    file->Filedes     = fd;
    file->Filemap     = map;
    file->FileSize    = file_size.QuadPart;
    file->Granularity = sys_info.dwAllocationGranularity;
    return 0;

cleanup_and_fail:
    if (map != NULL) CloseHandle(map);
    if (fd != INVALID_HANDLE_VALUE) CloseHandle(fd);
    return -1;
}

/// @summary Close a file mapping opened with OsOpenFileMapping. All views should have been unmapped already.
/// @param file The OS_FILE_MAPPING to close.
public_function void
OsCloseFileMapping
(
    OS_FILE_MAPPING *file
)
{
    if (file->Filemap != NULL)
    {
        CloseHandle(file->Filemap);
        file->Filemap = NULL;
    }
    if (file->Filedes != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file->Filedes);
        file->Filedes = INVALID_HANDLE_VALUE;
    }
}

/// @summary Map a region of a file into the process address space. The file is mapped read-only.
/// @param data The OS_FILE_DATA to populate with the mapped region.
/// @param offset The zero-based offset of the first byte within the file to map.
/// @param size The number of bytes to map into the process address space.
/// @param file The OS_FILE_MAPPING returned by a previous call to OsOpenFileMapping.
/// @return Zero if the region is successfully mapped into the process address space, or -1 if an error occurred.
public_function int
OsMapFileRegion
(
    OS_FILE_DATA    *data, 
    int64_t        offset, 
    int64_t          size, 
    OS_FILE_MAPPING *file
)
{   // the mapping offset must be an integer multiple of the system allocation granularity.
    // sys_offset is the starting offset of the view, adhering to this requirement.
    // sys_nbytes is the actual size of the view, in bytes, adjusted for the granularity requirement.
    // adjust is the byte adjustment between the start of the mapped region and what the user asked to see.
    int64_t sys_offset = (offset / file->Granularity) * file->Granularity;
    ptrdiff_t   adjust =  offset - sys_offset;
    int64_t sys_nbytes =  size   + adjust;
    DWORD         hofs = (DWORD)  (sys_offset >> 32);
    DWORD         lofs = (DWORD)  (sys_offset & 0xFFFFFFFFUL);
    DWORD        wsize = (DWORD) ((sys_offset + sys_nbytes > file->FileSize) ? 0 : sys_nbytes);
    void         *base =  MapViewOfFile(file->Filemap, FILE_MAP_READ, hofs, lofs, wsize);
    if (base == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to map region [%I64d, %I64d) (%08X).\n", __FUNCTION__, GetCurrentThreadId(), sys_offset, sys_offset+sys_nbytes, GetLastError());
        ZeroMemory(data, sizeof(OS_FILE_DATA));
        return -1;
    }
    data->Buffer   =((uint8_t*) base) + adjust;
    data->MapPtr   = base;
    data->Offset   = offset;
    data->DataSize = size;
    data->Flags    = OS_FILE_DATA_FLAG_MAPPED_REGION;
    return 0;
}

/// @summary Free resources associated with a loaded OS_FILE_DATA object.
/// @param data The OS_FILE_DATA to free.
public_function void
OsFreeFileData
(
    OS_FILE_DATA *data
)
{
    if ((data->MapPtr != NULL) && (data->Flags & OS_FILE_DATA_FLAG_MAPPED_REGION))
    {   // unmap the region, which will drop the pages.
        UnmapViewOfFile(data->MapPtr);
    }
    else if ((data->Buffer != NULL) && (data->Flags & OS_FILE_DATA_FLAG_COMMITTED))
    {   // free the buffer, which was allocated with malloc.
        free(data->Buffer);
    }
    else
    {   // nothing to do in this case.
        // ...
    }
    ZeroMemory(data, sizeof(OS_FILE_DATA));
}

/// @summary Calculate the amount of memory required to create an OS thread pool.
/// @param thread_count The number of threads in the thread pool.
/// @return The number of bytes required to create an OS_IO_THREAD_POOL with the specified number of worker threads. This value does not include the thread-local memory or thread stack memory.
public_function size_t
OsAllocationSizeForIoThreadPool
(
    size_t thread_count
)
{
    size_t size_in_bytes = 0;
    size_in_bytes += OsAllocationSizeForArray<unsigned int>(thread_count);
    size_in_bytes += OsAllocationSizeForArray<HANDLE>(thread_count);
    size_in_bytes += OsAllocationSizeForArray<HANDLE>(thread_count);
    size_in_bytes += OsAllocationSizeForArray<HANDLE>(thread_count);
    return size_in_bytes;
}

/// @summary Create a thread pool. The calling thread is blocked until all worker threads have successfully started and initialized.
/// @param pool The OS_IO_THREAD_POOL instance to initialize.
/// @param init An OS_IO_THREAD_POOL_INIT object describing the thread pool configuration.
/// @param arena The OS_MEMORY_ARENA from which all thread pool memory is allocated.
/// @param name A zero-terminated string constant specifying a human-readable name for the thread pool, or NULL. This name is used for task profiler display.
/// @return Zero if the thread pool is created successfully and worker threads are ready-to-run, or -1 if an error occurs.
public_function int
OsCreateIoThreadPool
(
    OS_IO_THREAD_POOL      *pool, 
    OS_IO_THREAD_POOL_INIT *init,
    OS_MEMORY_ARENA       *arena,
    char const             *name=NULL
)
{
    HANDLE                  iocp = NULL;
    HANDLE         evt_terminate = NULL;
    os_arena_marker_t mem_marker = OsMemoryArenaMark(arena);
    size_t        bytes_required = OsAllocationSizeForIoThreadPool(init->ThreadCount);
    size_t        align_required = std::alignment_of<HANDLE>::value;
    DWORD                    tid = GetCurrentThreadId();

    UNREFERENCED_PARAMETER(name);

    // Zero the fields of the OS_IO_THREAD_POOL instance to start from a known state.
    ZeroMemory(pool, sizeof(OS_IO_THREAD_POOL));

    if (!OsMemoryArenaCanSatisfyAllocation(arena, bytes_required, align_required))
    {
        OsLayerError("ERROR: %S(%u): Insufficient memory to create thread pool.\n", __FUNCTION__, tid);
        goto cleanup_and_fail;
    }
    if ((evt_terminate = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // without the termination event, there's no way to synchronize worker shutdown.
        OsLayerError("ERROR: %S(%u): Unable to create I/O pool termination event (0x%08X).\n", __FUNCTION__, tid, GetLastError());
        goto cleanup_and_fail;
    }
    if ((iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, (DWORD) init->ThreadCount+1)) == NULL)
    {   // without the completion port, there's no way to synchronize worker execution.
        OsLayerError("ERROR: %S(%u): Unable to create I/O pool completion port (0x%08X).\n", __FUNCTION__, tid, GetLastError());
        goto cleanup_and_fail;
    }

    // initialize the thread pool fields and allocate memory for per-thread arrays.
    pool->ActiveThreads    = 0;
    pool->OSThreadIds      = OsMemoryArenaAllocateArray<unsigned int>(arena, init->ThreadCount);
    pool->OSThreadHandle   = OsMemoryArenaAllocateArray<HANDLE      >(arena, init->ThreadCount);
    pool->WorkerReady      = OsMemoryArenaAllocateArray<HANDLE      >(arena, init->ThreadCount);
    pool->WorkerError      = OsMemoryArenaAllocateArray<HANDLE      >(arena, init->ThreadCount);
    if (pool->OSThreadIds == NULL || pool->OSThreadHandle == NULL || 
        pool->WorkerReady == NULL || pool->WorkerError    == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate I/O pool memory.\n", __FUNCTION__, tid);
        goto cleanup_and_fail;
    }
    pool->CompletionPort  = iocp;
    pool->TerminateSignal = evt_terminate;
    ZeroMemory(pool->OSThreadIds    , init->ThreadCount * sizeof(unsigned int));
    ZeroMemory(pool->OSThreadHandle , init->ThreadCount * sizeof(HANDLE));
    ZeroMemory(pool->WorkerReady    , init->ThreadCount * sizeof(HANDLE));
    ZeroMemory(pool->WorkerError    , init->ThreadCount * sizeof(HANDLE));

    // set up the worker init structure and spawn all threads.
    for (size_t i = 0, n = init->ThreadCount; i < n; ++i)
    {
        OS_IO_THREAD_INIT  winit  = {};
        HANDLE             whand  = NULL;
        HANDLE            wready  = NULL;
        HANDLE            werror  = NULL;
        unsigned int   thread_id  = 0;
        const DWORD THREAD_READY  = 0;
        const DWORD THREAD_ERROR  = 1;
        const DWORD   WAIT_COUNT  = 2;
        HANDLE   wset[WAIT_COUNT] = {};
        DWORD             waitrc  = 0;

        // create the manual-reset events signaled by the worker to indicate that it is ready.
        if ((wready = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to create ready signal for I/O worker %Iu of %Iu (0x%08X).\n", __FUNCTION__, tid, i, n, GetLastError());
            goto cleanup_and_fail;
        }
        if ((werror = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to create error signal for I/O worker %Iu of %Iu (0x%08X).\n", __FUNCTION__, tid, i, n, GetLastError());
            CloseHandle(wready);
            goto cleanup_and_fail;
        }

        // populate the IO_THREAD_INIT and then spawn the worker thread.
        // the worker thread will need to copy this structure if it wants to access it 
        // past the point where it signals the wready event.
        winit.ThreadPool      = pool;
        winit.ThreadInit      = init->ThreadInit;
        winit.ReadySignal     = wready;
        winit.ErrorSignal     = werror;
        winit.TerminateSignal = evt_terminate;
        winit.CompletionPort  = iocp;
        winit.PoolContext     = init->PoolContext;
        if ((whand = (HANDLE) _beginthreadex(NULL, Kilobytes(64), OsIoThreadMain, &winit, 0, &thread_id)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to spawn I/O worker %Iu of %Iu (errno = %d).\n", __FUNCTION__, tid, i, n, errno);
            CloseHandle(werror);
            CloseHandle(wready);
            goto cleanup_and_fail;
        }

        // save the various thread attributes in case 
        pool->OSThreadHandle[i] = whand;
        pool->OSThreadIds[i] = thread_id;
        pool->WorkerReady[i] = wready;
        pool->WorkerError[i] = werror;
        pool->ActiveThreads++;

        // wait for the thread to become ready.
        wset[THREAD_READY] = wready; 
        wset[THREAD_ERROR] = werror;
        if ((waitrc = WaitForMultipleObjects(WAIT_COUNT, wset, FALSE, INFINITE)) != (WAIT_OBJECT_0+THREAD_READY))
        {   // thread initialization failed, or the wait failed.
            // events are already in the IO_THREAD_POOL arrays, so don't clean up here.
            OsLayerError("ERROR: %S(%u): Failed to initialize I/O worker %Iu of %Iu (0x%08X).\n", __FUNCTION__, tid, i, n, waitrc);
            goto cleanup_and_fail;
        }
    }

    // everything has been successfully initialized. 
    // all worker threads are waiting on the completion port.
    return 0;

cleanup_and_fail:
    if (pool->ActiveThreads > 0)
    {   // signal all threads to terminate, and then wait until they all die.
        // all workers are blocked waiting on the launch event.
        SetEvent(evt_terminate);
        WaitForMultipleObjects((DWORD) pool->ActiveThreads, pool->OSThreadHandle, TRUE, INFINITE);
        // now that all threads have exited, close their handles.
        for (size_t i = 0, n = pool->ActiveThreads; i < n; ++i)
        {
            if (pool->OSThreadHandle != NULL) CloseHandle(pool->OSThreadHandle[i]);
            if (pool->WorkerReady    != NULL) CloseHandle(pool->WorkerReady[i]);
            if (pool->WorkerError    != NULL) CloseHandle(pool->WorkerError[i]);
        }
    }
    // clean up the I/O completion port and synchronization objects.
    if (evt_terminate) CloseHandle(evt_terminate);
    if (iocp) CloseHandle(iocp);
    // reset the memory arena back to its initial state.
    OsMemoryArenaResetToMarker(arena, mem_marker);
    // zero out the OS_IO_THREAD_POOL prior to returning to the caller.
    ZeroMemory(pool, sizeof(OS_IO_THREAD_POOL));
    return -1;
}

/// @summary Perform a fast shutdown of a thread pool. The calling thread does not wait for the worker threads to exit. No handles are closed.
/// @param pool The OS_IO_THREAD_POOL to shut down.
public_function void
OsTerminateIoThreadPool
(
    OS_IO_THREAD_POOL *pool
)
{
    if (pool->ActiveThreads > 0)
    {   // signal the termination event prior to waking any waiting threads.
        SetEvent(pool->TerminateSignal);
        // signal all worker threads in the pool. any active processing will complete before this signal is received.
        for (size_t i = 0; i < pool->ActiveThreads; ++i)
        {
            if (!PostQueuedCompletionStatus(pool->CompletionPort, 0, OS_COMPLETION_KEY_SHUTDOWN, NULL))
            {
                OsLayerError("ERROR: %S(%u): Failed to post shutdown signal to I/O worker %Iu (%08X).\n", __FUNCTION__, GetCurrentThreadId(), i, GetLastError());
            }
        }
    }
}

/// @summary Perform a complete shutdown and cleanup of a thread pool. The calling thread is blocked until all threads exit.
/// @param pool The OS_IO_THREAD_POOL to shut down and clean up.
public_function void
OsDestroyIoThreadPool
(
    OS_IO_THREAD_POOL *pool
)
{
    if (pool->ActiveThreads > 0)
    {   // signal the termination event prior to waking any waiting threads.
        SetEvent(pool->TerminateSignal);
        // signal all worker threads in the pool. any active processing will complete before this signal is received.
        for (size_t i = 0; i < pool->ActiveThreads; ++i)
        {
            if (!PostQueuedCompletionStatus(pool->CompletionPort, 0, OS_COMPLETION_KEY_SHUTDOWN, NULL))
            {
                OsLayerError("ERROR: %S(%u): Failed to post shutdown signal to I/O worker %Iu (%08X).\n", __FUNCTION__, GetCurrentThreadId(), i, GetLastError());
            }
        }
        // finally, wait for all threads to terminate gracefully.
        WaitForMultipleObjects((DWORD) pool->ActiveThreads, pool->OSThreadHandle, TRUE, INFINITE);
        // now that all threads have exited, close their handles.
        for (size_t i = 0, n = pool->ActiveThreads; i < n; ++i)
        {
            CloseHandle(pool->OSThreadHandle[i]);
            CloseHandle(pool->WorkerReady[i]);
            CloseHandle(pool->WorkerError[i]);
        }
        ZeroMemory(pool->OSThreadIds   , pool->ActiveThreads * sizeof(unsigned int));
        ZeroMemory(pool->OSThreadHandle, pool->ActiveThreads * sizeof(HANDLE));
        ZeroMemory(pool->WorkerReady   , pool->ActiveThreads * sizeof(HANDLE));
        ZeroMemory(pool->WorkerError   , pool->ActiveThreads * sizeof(HANDLE));
        pool->ActiveThreads = 0;
    }
    if (pool->TerminateSignal != NULL)
    {
        CloseHandle(pool->TerminateSignal);
        pool->TerminateSignal = NULL;
    }
    if (pool->CompletionPort != NULL)
    {
        CloseHandle(pool->CompletionPort);
        pool->CompletionPort = NULL;
    }
}

/// @summary Calculate the amount of memory required for an I/O request pool.
/// @param pool_capacity The maximum number of I/O requests that can be allocated from the pool at any one time.
/// @return The number of bytes required for the I/O request pool.
public_function size_t
OsAllocationSizeForIoRequestPool
(
    size_t pool_capacity
)
{
    return OsAllocationSizeForArray<OS_IO_REQUEST_POOL>(pool_capacity);
}

/// @summary Create an I/O request pool.
/// @param pool The I/O request pool to initialize.
/// @param arena The memory arena from which the request pool will be allocated.
/// @param pool_capacity The maximum number of I/O requests that can be allocated from the pool at any one time.
/// @return Zero if the pool is successfully initialized, or -1 if an error occurred.
public_function int
OsCreateIoRequestPool
(
    OS_IO_REQUEST_POOL *pool, 
    OS_MEMORY_ARENA   *arena, 
    size_t     pool_capacity
)
{
    OS_IO_REQUEST *node_pool = NULL;

    // initialize the fields of the OS_IO_REQUEST_POOL instance.
    ZeroMemory(pool, sizeof(OS_IO_REQUEST_POOL));

    // allocate and initialize the pool nodes and free list.
    if ((node_pool = OsMemoryArenaAllocateArray<OS_IO_REQUEST>(arena, pool_capacity)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate I/O request pool of %Iu items.\n", __FUNCTION__, GetCurrentThreadId(), pool_capacity);
        return -1;
    }
    ZeroMemory(node_pool,  pool_capacity * sizeof(OS_IO_REQUEST));
    for (size_t i = 0; i < pool_capacity; ++i)
    {
        node_pool[i].NextRequest = pool->FreeRequest;
        pool->FreeRequest = &node_pool[i];
    }
    InitializeCriticalSectionAndSpinCount(&pool->ListLock, 0x1000);
    pool->NodePool = node_pool;
    return 0;
}

/// @summary Allocate an I/O request from an I/O request pool.
/// @param pool The I/O request pool to allocate from.
/// @return The I/O request, or NULL if no requests are available in the pool.
public_function OS_IO_REQUEST*
OsAllocateIoRequest
(
    OS_IO_REQUEST_POOL *pool
)
{
    OS_IO_REQUEST *node = NULL;
    EnterCriticalSection(&pool->ListLock);
    {
        if ((node = pool->FreeRequest) != NULL)
        {   // pop a node from the head of the free list.
            pool->FreeRequest      = pool->FreeRequest->NextRequest;
            // insert the node at the head of the live list.
            node->NextRequest      = pool->LiveRequest;
            node->PrevRequest      = NULL;
            node->RequestPool      = pool;
            if (pool->LiveRequest != NULL)
                pool->LiveRequest->PrevRequest = node;
            pool->LiveRequest      = node;
            node->RequestState     = OS_IO_REQUEST_STATE_CHAINED;
        }
    }
    LeaveCriticalSection(&pool->ListLock);
    return node;
}

/// @summary Submit an I/O request for asynchronous execution.
/// @param io_pool The I/O thread pool that will execute the asynchronous I/O request.
/// @param request The I/O request to submit to the queue. The request will be executed by a worker thread.
/// @return true if the request is successfully submitted.
public_function bool
OsSubmitIoRequest
(
    OS_IO_THREAD_POOL *io_pool, 
    OS_IO_REQUEST     *request
)
{
    request->RequestState = OS_IO_REQUEST_STATE_SUBMITTED;
    request->IoSubmitTime = OsTimestampInTicks();
    if (!PostQueuedCompletionStatus(io_pool->CompletionPort, 0, 0, &request->Overlapped))
    {
        OsReturnIoRequest(request);
        OsLayerError("ERROR: %S(%u): Failed to submit request %p from pool %p (%08X).\n", __FUNCTION__, GetCurrentThreadId(), request, request->RequestPool, GetLastError());
        return false;
    }
    return true;
}

