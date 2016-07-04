/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement the OS layer services for the Win32 platform, including
/// the following functionality:
/// - Memory arena (Memory resource management)
/// - Thread pool (CPU resource management)
/// - Disk I/O (Asynchronous storage resource management)
/// - HID I/O (User input device management)
/// - Low-latency audio (audio input and output device management)
/// - Vulkan WSI (Vulkan Window system interface and swap chain management)
///////////////////////////////////////////////////////////////////////////80*/

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
#define OsThreadEvent(pool, fmt, ...)               CvWriteAlertW((pool)->TaskProfiler.MarkerSeries, _T(fmt), __VA_ARGS__)
#define OsThreadSpanEnter(pool, span, fmt, ...)     CvEnterSpanW((pool)->TaskProfiler.MarkerSeries, &(span).CvSpan, _T(fmt), __VA_ARGS__)
#define OsThreadSpanLeave(pool, span)               CvLeaveSpan((span).CvSpan)

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
/// @param vkloader The OS_VULKAN_LOADER maintaining the function pointer to the entry point.
/// @param fname The entry point to resolve.
#ifndef OS_LAYER_RESOLVE_VULKAN_ICD_FUNCTION
#define OS_LAYER_RESOLVE_VULKAN_ICD_FUNCTION(vkloader, fname) \
        do {                                                                   \
            if (((vkloader)->fname = (PFN_##fname) GetProcAddress((vkloader)->LoaderHandle, #fname)) == NULL) { \
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
/// @param vkloader The OS_VULKAN_LOADER maintaining the function pointer to the entry point.
/// @param fname The entry point to resolve.
#ifndef OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION
    #define OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(vkloader, fname)           \
        do {                                                                   \
            if (((vkloader)->fname = (PFN_##fname) (vkloader)->vkGetInstanceProcAddr(NULL, #fname)) == NULL) { \
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
/// @param vkinstance The OS_VULKAN_INSTANCE maintaining the function pointer to the entry point.
/// @param vkloader The OS_VULKAN_LOADER maintaining the function pointer to the vkGetInstanceProcAddr entry point.
/// @param fname The entry point to resolve.
#ifndef OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION
    #define OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, fname) \
        do {                                                                   \
            if (((vkinstance)->fname = (PFN_##fname)(vkloader)->vkGetInstanceProcAddr((vkinstance)->InstanceHandle, #fname)) == NULL) { \
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
/// @param fn The Vulkan API function to resolve.
/// @param device_obj The VK_DEVICE maintaining the function pointer to set.
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

/*////////////////
//   Includes   //
////////////////*/
#ifndef OS_LAYER_NO_INCLUDES
    #include <type_traits>

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
struct OS_MEMORY_ARENA;

struct OS_WORKER_THREAD;
struct OS_THREAD_POOL;
struct OS_THREAD_POOL_INIT;

struct OS_FILE;
struct OS_MOUNTPOINT;
struct OS_FILE_REGION;

struct OS_INPUT_SYSTEM;
struct OS_INPUT_EVENTS;
struct OS_POINTER_EVENTS;
struct OS_GAMEPAD_EVENTS;
struct OS_KEYBOARD_EVENTS;

struct OS_TASK_PROFILER;
struct OS_TASK_PROFILER_SPAN;

struct OS_VULKAN_DEVICE;
struct OS_VULKAN_RUNTIME;
struct OS_VULKAN_INSTANCE;
struct OS_VULKAN_RUNTIME_PROPERTIES;
struct OS_VULKAN_PHYSICAL_DEVICE_LIST;

struct OS_AUDIO_SYSTEM;
struct OS_AUDIO_DEVICE_LIST;
struct OS_AUDIO_OUTPUT_DEVICE;
struct OS_AUDIO_CAPTURE_DEVICE;

/// @summary Define the data associated with an operating system arena allocator. 
/// The memory arena is not safe for concurrent access by multiple threads.
struct OS_MEMORY_ARENA
{
    size_t              NextOffset;                  /// The offset, in bytes relative to BaseAddress, of the next available byte.
    size_t              BytesCommitted;              /// The number of bytes committed for the arena.
    size_t              BytesReserved;               /// The number of bytes reserved for the arena.
    uint8_t            *BaseAddress;                 /// The base address of the reserved segment of process virtual address space.
    size_t              ReserveAlignBytes;           /// The number of alignment-overhead bytes for the current user reservation.
    size_t              ReserveTotalBytes;           /// The total size of the current user reservation, in bytes.
    DWORD               PageSize;                    /// The operating system page size.
    DWORD               Granularity;                 /// The operating system allocation granularity.
};

/// @summary Alias type for a marker within a memory arena.
typedef uintptr_t       os_arena_marker_t;           /// The marker stores the value of the OS_MEMORY_ARENA::NextOffset field at a given point in time.

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

/// @summary Define the data available to an application callback executing on a worker thread.
struct OS_WORKER_THREAD
{
    OS_THREAD_POOL     *ThreadPool;                  /// The thread pool that manages the worker.
    OS_MEMORY_ARENA    *ThreadArena;                 /// The thread-local memory arena.
    HANDLE              CompletionPort;              /// The I/O completion port used to wait and wake the thread.
    void               *PoolContext;                 /// The opaque, application-specific data passed through to the thread.
    void               *ThreadContext;               /// The opaque, application-specific data created by the OS_WORKER_INIT callback for the thread.
    size_t              ArenaSize;                   /// The size of the thread-local memory arena, in bytes.
    unsigned int        ThreadId;                    /// The operating system identifier for the thread.
};

/// @summary Define the signature for the callback invoked during worker thread initialization to allow the application to create any per-thread resources.
/// @param thread_args An OS_WORKER_THREAD instance specifying worker thread data. The callback should set the ThreadContext field to its private data.
/// @return Zero if initialization was successful, or -1 to terminate the worker thread.
typedef int           (*OS_WORKER_INIT)(OS_WORKER_THREAD *thread_args);

/// @summary Define the signature for the callback representing the application entry point on a worker thread.
/// @param thread_args An OS_WORKER_THREAD instance, valid until the OS_WORKER_ENTRY returns, specifying per-thread data.
/// @param signal_arg An application-defined value specified with the wake notification.
/// @param wake_reason One of OS_WORKER_THREAD_WAKE_REASON indicating the reason the thread was woken.
typedef void          (*OS_WORKER_ENTRY)(OS_WORKER_THREAD *thread_args, uintptr_t signal_arg, int wake_reason);

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

#pragma pack(push, 1)
/// @summary Defines the fields of the base TAR header as they exist in the file.
struct OS_TAR_HEADER_ENCODED
{
    char                FileName[100];               /// The filename, with no path information.
    char                FileMode[8];                 /// The octal file mode value.
    char                OwnerUID[8];                 /// The octal owner user ID value.
    char                GroupUID[8];                 /// The octal group ID value.
    char                FileSize[12];                /// The octal file size, in bytes.
    char                FileTime[12];                /// The octal UNIX file time of last modification.
    char                Checksum[8];                 /// The octal checksum value of the file.
    char                FileType;                    /// One of the values of the OS_VFS_TAR_ENTRY_TYPE enumeration.
    char                LinkName[100];               /// The filename of the linked file, with no path information.
    char                ExtraPad[255];               /// Additional unused data, or, possibly, a OS_USTAR_HEADER_ENCODED.
};
#pragma pack(pop)

#pragma pack(push, 1)
/// @summary Defines the fields of the extended USTAR header as they exist in the file.
struct OS_USTAR_HEADER_ENCODED
{
    char                MagicStr[6];                 /// The string "ustar\0".
    char                Version [2];                 /// The version number, terminated with a space.
    char                OwnerStr[32];                /// The ASCII name of the file owner.
    char                GroupStr[32];                /// The ASCII name of the file group.
    char                DevMajor[8];                 /// The octal device major number.
    char                DevMinor[8];                 /// The octal device minor number.
    char                FileBase[155];               /// The prefix value for the FileName, with no trailing slash.
};
#pragma pack(pop)

/// @summary Defines the set of information maintained at runtime for each entry in a tarball.
struct OS_TAR_ENTRY
{
    int64_t             FileSize;                    /// The size of the file, in bytes.
    uint64_t            FileTime;                    /// The last write time of the file, as a UNIX timestamp.
    int64_t             DataOffset;                  /// The absolute byte offset to the start of the file data.
    uint32_t            Checksum;                    /// The checksum of the file data.
    uint32_t            Reserved;                    /// Reserved for future use. Set to 0.
    char                FileType;                    /// One of the values of the tar_entry_type_e enumeration.
    char                FullPath[257];               /// The full path of the file, "<FileBase>/<FileName>\0".
    char                LinkName[101];               /// The filename of the linked file, zero-terminated.
    char                Padding;                     /// One extra byte of padding. Set to 0.
};

/// @summary Defines the metadata associated with a tarball.
struct OS_TARBALL
{
    size_t              EntryCount;                  /// The number of regular file entries defined in the tarball.
    uint32_t           *EntryHash;                   /// An array where each item [Entry] specifies the hash of the entry filename.
    OS_TAR_ENTRY       *EntryInfo;                   /// An array where each item [Entry] specifies information about a single file entry.
};

/// @summary Define the signature for the callback invoked to synchronously open a file for access. The information necessary to access the file is written to @a file.
/// @param mount The mount point performing the file open operation.
/// @param path The zero-terminated UTF-8 path of the file to open, relative to the mount point root.
/// @param usage One of OS_FILE_USAGE specifying the intended use of the file by the application.
/// @param hints One or more of OS_FILE_USAGE_HINTS specifying additional desired behaviors.
/// @param file If the call returns OS_IO_RESULT_SUCCESS, this structure should be populated with file data.
/// @return One of OS_IO_RESULT indicating the result of the operation.
typedef int           (*OS_OPEN_FILE)(OS_MOUNTPOINT *mount, char const *path, int usage, uint32_t hints, OS_FILE *file);

/// @summary Define the signature for the callback invoked to synchronously write an entire file to disk. If the file exists, its contents are overwritten; otherwise, a new file is created.
/// @param mount The mount point performing the file save operation.
/// @param path The zero-terminated UTF-8 path of the file to write, relative to the mount point root.
/// @param data The buffer containing the data to write to the file.
/// @param file_size The number of bytes to read from the data buffer and write to the file.
/// @return One of OS_IO_RESULT indicating the result of the operation.
typedef int           (*OS_SAVE_FILE)(OS_MOUNTPOINT *mount, char const *path, void const *data, int64_t const file_size);

/// @summary Define the signature for the callback invoked to determine whether a given mount point supports a particular type of file access.
/// @param mount The mount point to query.
/// @param usage One of OS_FILE_USAGE specifying the intended use of the file by the application.
/// @return OS_IO_RESULT_SUCCESS if the usage is supported, or OS_IO_RESULT_NOT_SUPPORTED.
typedef int           (*OS_MOUNT_SUPPORT)(OS_MOUNTPOINT *mount, int usage);

/// @summary Define the signature for the callback invoked when a mount point is being removed.
/// @param mount The mount point being unmounted.
typedef void          (*OS_UNMOUNT)(OS_MOUNTPOINT *mount);

/// @summary Defines the data associated with an open file. This information is populated by the mount point.
struct OS_FILE
{
    HANDLE              Filedes;                     /// The handle used to access the file.
    HANDLE              Filemap;                     /// The handle representing the file mapping, for memory-mapped files.
    size_t              SectorSize;                  /// The physical sector size of the disk, in bytes.
    int64_t             BaseOffset;                  /// The offset of the start of the file data, in butes.
    int64_t             BaseSize;                    /// The size of the file data, in bytes, as stored.
    int64_t             FileSize;                    /// The runtime size of the file data, in bytes. For non-compressed files, BaseSize and FileSize are the same.
    uint32_t            FileHints;                   /// One or more of OS_FILE_USAGE_HINTS.
    uint32_t            FileFlags;                   /// Reserved for future use. Set to 0.
    DWORD               OSError;                     /// The most recent operating system error reported when accessing the file.
    DWORD               OpenFlags;                   /// The open mode flags used to open the file handle. See MSDN for CreateFile.
};

/// @summary Defines the data associated with a memory-mapped region of a file.
struct OS_FILE_REGION
{
    int64_t             BaseOffset;                  /// The zero-based offset from the start of the file at which the mapped region begins. This is always a multiple of the system allocation granularity.
    uint8_t            *SystemBase;                  /// The pointer to the start of the mapped region, aligned to the system allocation granularity.
    uint8_t            *UserBase;                    /// The pointer to the start of the mapped region requested by the user.
    int64_t             RegionSize;                  /// The number of bytes mapped, starting at UserBase.
};

/// @summary Define the data associated with a mountpoint, which represents a single mounted file system within the larger virtualized file system.
/// The file system could be represented by a local directory, a remote directory, or an archive file.
struct OS_MOUNTPOINT
{
    uintptr_t           Id;                          /// The application-defined mount point identifier.
    char               *Root;                        /// The mount point name exposed to the VFS, such as "/exec".
    size_t              RootLength;                  /// The length of the mount point name, in bytes.
    size_t              LocalPathLength;             /// The length of the LocalPath string, in characters.
    WCHAR              *LocalPath;                   /// The fully-resolved absolute path of the backing directory or file.
    void               *StateData;                   /// A pointer to opaque mountpoint-specific data.
    OS_OPEN_FILE        OpenFile;                    /// The callback used to open a file for access.
    OS_SAVE_FILE        SaveFile;                    /// The callback used to save a file to disk.
    OS_MOUNT_SUPPORT    SupportsUsage;               /// The callback used to determine whether the mountpoint supports a given file usage.
    OS_UNMOUNT          Unmount;                     /// The callback used to clean up mountpoint-specific data when the mount point is removed.
};

/// @summary Defines the internal state data associated with a tarball mount point.
struct OS_MOUNTPOINT_TAR
{
    HANDLE              TarFiledes;                  /// The file descriptor for the tarball. Duplicated for each opened file.
    size_t              SectorSize;                  /// The physical disk sector size, in bytes.
    OS_TARBALL          TarballData;                 /// The parsed entry data for all regular files.
};

/// @summary Define the data associated with a prioritized set of mount points. Items can be iterated over in priority order. Multiple mount points can be defined at the same root location, but with different priorities.
struct OS_MOUNTPOINT_TABLE
{
    size_t              Count;                       /// The number of mountpoints defined in the table.
    size_t              Capacity;                    /// The capacity of the mount point table. The capacity is fixed.
    uintptr_t          *MountpointIds;               /// The set of application-defined unique mount point identifiers.
    OS_MOUNTPOINT      *MountpointData;              /// The set of mount point data and callback functions.
    uint32_t           *MountpointPriority;          /// The set of mount point priority values.
};

/// @summary Define the data associated with the virtual file system.
struct OS_FILE_SYSTEM
{
    SRWLOCK             MountpointLock;              /// A reader-writer lock protecting access to the mountpoint table.
    OS_MOUNTPOINT_TABLE MountpointTable;             /// A table of mount points stored in priority order.
    DWORD               PageSize;                    /// The operating system page size, in bytes.
    DWORD               AllocationGranularity;       /// The operating system allocation granularity, in bytes.
    // ...
};

/// @summary Define the parameters used to configure the file system interface.
struct OS_FILE_SYSTEM_INIT
{
    size_t              MaxMountPoints;              /// The maximum number of active mount points.
    size_t              MaxQueuedIoOps;              /// The maximum number of queued asynchronous I/O operations.
    size_t              MaxInFlightIoOps;            /// The maximum number of in-flight asynchronous I/O operations.
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

/// @summary Defines the data associated with the top-level Vulkan entry points used to load the Vulkan API.
struct OS_VULKAN_RUNTIME
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
struct OS_VULKAN_INSTANCE
{
    VkInstance                        InstanceHandle;                                /// The Vulkan instance used by the application to interface with the Vulkan API.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkCreateDevice);                               /// The vkCreateDevice function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkDestroyInstance);                            /// The vkDestroyInstance function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkEnumeratePhysicalDevices);                   /// The vkEnumeratePhysicalDevices function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkEnumerateDeviceExtensionProperties);         /// The vkEnumerateDeviceExtensionProperties function.
    OS_LAYER_VULKAN_INSTANCE_FUNCTION(vkEnumerateDeviceLayerProperties);             /// The vkEnumerateDeivceLayerProperties function.
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
    size_t                           *DeviceLayerCount;                              /// An array where each element [Device] specifies the number of device-level layers exposed by the runtime for a physical device.
    VkLayerProperties               **DeviceLayerProperties;                         /// An array where each element [Device][Layer] specifies the properties of a single device-level runtime layer for a physical device.
    size_t                          **DeviceLayerExtensionCount;                     /// An array where each element [Device][Layer] specifies the number of extensions for a single device-level runtime layer for a physical device.
    VkExtensionProperties          ***DeviceLayerExtensionProperties;                /// An array where each element [Device][Layer][Extension] specifies the properties of a single device-level runtime layer extension for a physical device.
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
struct OS_VULKAN_DEVICE
{
    VkDevice                          DeviceHandle;                                  /// The Vulkan logical device handle used to interface with device-level API functions.
    VkPhysicalDevice                  PhysicalDeviceHandle;                          /// The handle of the Vulkan physical device that executes the application commands.
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

/// @summary Define the recognized categories of file system mount points.
enum OS_MOUNTPOINT_TYPE              : int32_t
{
    OS_MOUNTPOINT_TYPE_DIRECTORY     = 0,            /// The mount point is a directory on the filesystem.
    OS_MOUNTPOINT_TYPE_ARCHIVE       = 1,            /// The mount point is an archive file (like a TAR.)
    OS_MOUNTPOINT_TYPE_COUNT         = 2,            /// The number of recognized virtual file system mount point types.
};

/// @summary Define the recognized types of file system entities.
enum OS_FILE_ENTRY_TYPE              : int32_t
{
    OS_FILE_ENTRY_TYPE_UNKNOWN       = 0,            /// The entry type is not yet known or could not be determined.
    OS_FILE_ENTRY_TYPE_FILE          = 1,            /// The entry is a regular file.
    OS_FILE_ENTRY_TYPE_DIRECTORY     = 2,            /// The entry is a regular directory.
    OS_FILE_ENTRY_TYPE_ALIAS         = 3,            /// The entry is an alias to another file or directory.
    OS_FILE_ENTRY_TYPE_IGNORE        = 4,            /// The entry exists, but should be ignored.
    OS_FILE_ENTRY_TYPE_COUNT         = 5,            /// The number of recognized virtual file system entity types.
};

/// @summary Define the recognized usage types for file I/O, which in turn define the access and sharing modes.
enum OS_FILE_USAGE                   : int
{
    OS_FILE_USAGE_STREAM_IN          = 0,            /// The file will be streamed in to memory and then closed (read-only.)
    OS_FILE_USAGE_STREAM_OUT         = 1,            /// The file will be streamed out to disk and then closed (write-only.)
    OS_FILE_USAGE_MMAP_READ          = 2,            /// The file (or portions of the file) will be read-only using memory-mapped I/O.
    OS_FILE_USAGE_MMAP_WRITE         = 3,            /// The file (or portions of the file) will be read+write using memory-mapped I/O.
    OS_FILE_USAGE_MANUAL_IO          = 4,            /// The file will be opened for reading and writing using manual I/O.
};

/// @summary Define the return codes for file system I/O operations.
enum OS_IO_RESULT                    : int
{
    OS_IO_RESULT_SUCCESS             = 0,            /// The I/O operation completed successfully.
    OS_IO_RESULT_NOT_SUPPORTED       = 1,            /// The I/O system operation is not supported by the mountpoint driver.
    OS_IO_RESULT_OSERROR             = 2,            /// The I/O operation failed due to an operating system error. Check OS_FILE::OSError.
};

/// @summary Defines the recognized types of entries in a tarball. Vendor extensions may additionally have a type 'A'-'Z'.
enum OS_TAR_ENTRY_TYPE               : char
{
    OS_TAR_ENTRY_TYPE_FILE           = '0',          /// The entry represents a regular file.
    OS_TAR_ENTRY_TYPE_HARDLINK       = '1',          /// The entry represents a hard link.
    OS_TAR_ENTRY_TYPE_SYMLINK        = '2',          /// The entry represents a symbolic link.
    OS_TAR_ENTRY_TYPE_CHARACTER      = '3',          /// The entry represents a character device file.
    OS_TAR_ENTRY_TYPE_BLOCK          = '4',          /// The entry represents a block device file.
    OS_TAR_ENTRY_TYPE_DIRECTORY      = '5',          /// The entry represents a directory.
    OS_TAR_ENTRY_TYPE_FIFO           = '6',          /// The entry represents a named pipe file.
    OS_TAR_ENTRY_TYPE_CONTIGUOUS     = '7',          /// The entry represents a contiguous file.
    OS_TAR_ENTRY_TYPE_GMETA          = 'g',          /// The entry is a global extended header with metadata.
    OS_TAR_ENTRY_TYPE_XMETA          = 'x',          /// The entry is an extended header with metadata for the next file.
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

/// @summary Define flags used to optimize file accesses for a particular usage scenario.
enum OS_FILE_USAGE_HINTS             : uint32_t
{
    OS_FILE_USAGE_HINT_NONE          = (0 << 0),     /// No special usage hints are specified.
    OS_FILE_USAGE_HINT_UNBUFFERED    = (1 << 0),     /// Prefer to perform I/O that bypasses the system file cache.
    OS_FILE_USAGE_HINT_ASYNCHRONOUS  = (1 << 1),     /// All read or write I/O operations will be performed asynchronously.
    OS_FILE_USAGE_HINT_OVERWRITE     = (1 << 2),     /// If the file exists, the existing contents will be overwritten.
};

/// @summary Define flags indicating how to interpret WIN32_POINTER_STATE::Relative.
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
global_variable HMODULE    XInputDll = NULL;

/// @summary The GUID of the Win32 OS Layer task profiler provider {349CE0E9-6DF5-4C25-AC5B-C84F529BC0CE}.
global_variable GUID const TaskProfilerGUID = { 0x349ce0e9, 0x6df5, 0x4c25, { 0xac, 0x5b, 0xc8, 0x4f, 0x52, 0x9b, 0xc0, 0xce } };

/*////////////////////////////
//   Forward Declarations   //
////////////////////////////*/
public_function void              OsZeroMemory(void *dst, size_t len);
public_function void              OsSecureZeroMemory(void *dst, size_t len);
public_function void              OsCopyMemory(void * __restrict dst, void const * __restrict src, size_t len);
public_function void              OsMoveMemory(void *dst, void const *src, size_t len);
public_function void              OsFillMemory(void *dst, size_t len, uint8_t val);
public_function size_t            OsAlignUp(size_t size, size_t pow2);
public_function int               OsCreateMemoryArena(OS_MEMORY_ARENA *arena, size_t arena_size, bool commit_all, bool guard_page);
public_function void              OsDeleteMemoryArena(OS_MEMORY_ARENA *arena);
public_function size_t            OsMemoryArenaBytesReserved(OS_MEMORY_ARENA *arena);
public_function size_t            OsMemoryArenaBytesUncommitted(OS_MEMORY_ARENA *arena);
public_function size_t            OsMemoryArenaBytesCommitted(OS_MEMORY_ARENA *arena);
public_function size_t            OsMemoryArenaBytesInActiveReservation(OS_MEMORY_ARENA *arena);
public_function size_t            OsMemoryArenaPageSize(OS_MEMORY_ARENA *arena);
public_function size_t            OsMemoryArenaSystemGranularity(OS_MEMORY_ARENA *arena);
public_function bool              OsMemoryArenaCanSatisfyAllocation(OS_MEMORY_ARENA *arena, size_t alloc_size, size_t alloc_alignment);
public_function void*             OsMemoryArenaAllocate(OS_MEMORY_ARENA *arena, size_t alloc_size, size_t alloc_alignment);
public_function void*             OsMemoryArenaReserve(OS_MEMORY_ARENA *arena, size_t reserve_size, size_t alloc_alignment);
public_function int               OsMemoryArenaCommit(OS_MEMORY_ARENA *arena, size_t commit_size);
public_function void              OsMemoryArenaCancel(OS_MEMORY_ARENA *arena);
public_function os_arena_marker_t OsMemoryArenaMark(OS_MEMORY_ARENA *arena);
public_function void              OsMemoryArenaResetToMarker(OS_MEMORY_ARENA *arena, os_arena_marker_t arena_marker);
public_function void              OsMemoryArenaReset(OS_MEMORY_ARENA *arena);
public_function void              OsMemoryArenaDecommitToMarker(OS_MEMORY_ARENA *arena, os_arena_marker_t arena_marker);
public_function void              OsMemoryArenaDecommit(OS_MEMORY_ARENA *arena);

public_function uint64_t          OsTimestampInTicks(void);
public_function uint64_t          OsTimestampInNanoseconds(void);
public_function uint64_t          OsNanosecondSliceOfSecond(uint64_t fraction);
public_function uint64_t          OsElapsedNanoseconds(uint64_t start_ticks, uint64_t end_ticks);
public_function uint64_t          OsMillisecondsToNanoseconds(uint32_t milliseconds);
public_function uint32_t          OsNanosecondsToWholeMillisecons(uint64_t nanoseconds);
public_function bool              OsQueryHostCpuLayout(OS_CPU_INFO *cpu_info, OS_MEMORY_ARENA *arena);

public_function uint32_t __cdecl  OsWorkerThreadMain(void *argp);
public_function size_t            OsCalculateMemoryForThreadPool(size_t thread_count);
public_function int               OsCreateThreadPool(OS_THREAD_POOL *pool, OS_THREAD_POOL_INIT *init, OS_MEMORY_ARENA *arena, char const *name);
public_function void              OsLaunchThreadPool(OS_THREAD_POOL *pool);
public_function void              OsTerminateThreadPool(OS_THREAD_POOL *pool);
public_function void              OsDestroyThreadPool(OS_THREAD_POOL *pool);
public_function bool              OsSignalWorkerThreads(OS_WORKER_THREAD *sender, uintptr_t signal_arg, size_t thread_count);
public_function bool              OsSignalWorkerThreads(OS_THREAD_POOL *pool, uintptr_t signal_arg, size_t thread_count);

public_function int               OsLoadTarball(OS_TARBALL *tar, HANDLE tarfd, DWORD result);
public_function void              OsUnloadTarball(OS_TARBALL *tar);

public_function void              OsResetInputSystem(OS_INPUT_SYSTEM *system);
public_function void              OsPushRawInput(OS_INPUT_SYSTEM *system, RAWINPUT const *input);
public_function void              OsPushRawInputDeviceChange(OS_INPUT_SYSTEM *system, WPARAM wparam, LPARAM lparam);
public_function void              OsSimulateKeyPress(OS_INPUT_SYSTEM *system, HANDLE device, UINT virtual_key);
public_function void              OsSimulateKeyRelease(OS_INPUT_SYSTEM *system, HANDLE device, UINT virtual_key);
public_function void              OsConsumeInputEvents(OS_INPUT_EVENTS *events, OS_INPUT_SYSTEM *system, uint64_t tick_time);

public_function VkResult          OsLoadVulkanRuntime(OS_VULKAN_RUNTIME *runtime);
public_function VkResult          OsQueryVulkanRuntimeProperties(OS_VULKAN_RUNTIME_PROPERTIES *props, OS_VULKAN_RUNTIME *runtime, OS_MEMORY_ARENA *arena);
public_function VkResult          OsCreateVulkanInstance(OS_VULKAN_INSTANCE *instance, OS_VULKAN_RUNTIME *runtime, VkInstanceCreateInfo const *create_info, VkAllocationCallbacks const *allocation_callbacks);
public_function bool              OsIsPrimaryDisplay(OS_VULKAN_PHYSICAL_DEVICE_LIST const *device_list, size_t display_index);
public_function int32_t           OsDisplayRefreshRate(OS_VULKAN_PHYSICAL_DEVICE_LIST const *device_list, size_t display_index);
public_function char const*       OsSupportsVulkanInstanceLayer(OS_VULKAN_RUNTIME_PROPERTIES const *props, char const *layer_name, size_t *layer_index);
public_function bool              OsSupportsAllVulkanInstanceLayers(OS_VULKAN_RUNTIME_PROPERTIES const *props, char const **layer_name, size_t const layer_count);
public_function char const*       OsSupportsVulkanInstanceExtension(OS_VULKAN_RUNTIME_PROPERTIES const *props, char const *extension_name, size_t *extension_index);
public_function bool              OsSupportsAllVulkanInstanceExtensions(OS_VULKAN_RUNTIME_PROPERTIES const *props, char const **extension_names, size_t const extension_count);
public_function VkResult          OsEnumerateVulkanPhysicalDevices(OS_VULKAN_PHYSICAL_DEVICE_LIST *device_list, OS_VULKAN_INSTANCE *instance, OS_MEMORY_ARENA *arena, HINSTANCE exe_instance);
public_function VkResult          OsCreateVulkanLogicalDevice(OS_VULKAN_DEVICE *device, OS_VULKAN_INSTANCE *instance, VkPhysicalDevice physical_Device, VkDeviceCreateInfo const *create_info, VkAllocationCallbacks const *allocation_callbacks);

public_function int               OsInitializeAudio(OS_AUDIO_SYSTEM *audio_system);
public_function int               OsEnumerateAudioDevices(OS_AUDIO_DEVICE_LIST *device_list, OS_AUDIO_SYSTEM *audio_system, OS_MEMORY_ARENA *arena);
public_function void              OsDisableAudioOutput(OS_AUDIO_SYSTEM *audio_system);
public_function int               OsEnableAudioOutput(OS_AUDIO_SYSTEM *audio_system, WCHAR *device_id, uint32_t samples_per_second, uint32_t buffer_size);
public_function int               OsRecoverLostAudioOutputDevice(OS_AUDIO_SYSTEM *audio_system);
public_function uint32_t          OsAudioSamplesToWrite(OS_AUDIO_SYSTEM *audio_system);
public_function int               OsWriteAudioSamples(OS_AUDIO_SYSTEM *audio_system, void const *sample_data, uint32_t const sample_count);

public_function bool              OsShellFolderPath(WCHAR *buf, size_t buf_bytes, size_t &bytes_needed, REFKNOWNFOLDERID folder_id);
public_function bool              OsKnownPath(WCHAR *buf, size_t buf_bytes, size_t &bytes_needed, int folder_id);
public_function size_t            OsPhysicalSectorSize(HANDLE file);
public_function int               OsCreateFilesystem(OS_FILE_SYSTEM *file_system, OS_FILE_SYSTEM_INIT const *init, OS_MEMORY_ARENA *arena);
public_function int               OsMountKnownPath(OS_FILE_SYSTEM *file_system, int folder_id, char const *mount_path, uint32_t priority, uintptr_t mount_id);
public_function int               OsMountPhysicalPath(OS_FILE_SYSTEM *file_system, char const *source_path, char const *mount_path, uint32_t priority, uintptr_t mount_id);
public_function int               OsMountVirtualPath(OS_FILE_SYSTEM *file_system, char const *virtual_path, char const *mount_path, uint32_t priority, uintptr_t mount_id);
public_function void              OsUnmount(OS_FILE_SYSTEM *file_system, uintptr_t mount_id);
public_function void              OsUnmountAll(OS_FILE_SYSTEM *file_system, char const *mount_path);
public_function void              OsCloseFile(OS_FILE *file);
public_function int               OsOpenFile(OS_FILE_SYSTEM *file_system, char const *path, int usage, uint32_t hints, OS_FILE *file);
public_function int               OsReadFile(OS_FILE *file, int64_t offset, void *buffer, size_t size, size_t &bytes_read);
public_function int               OsWriteFile(OS_FILE *file, int64_t offset, void const *buffer, size_t size, size_t &bytes_written);
public_function int               OsFlushFile(OS_FILE *file);

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
{
    return (ch >= 'a' && ch <= 'z') ? (ch - 32) : ch;
}

/// @summary Convert an uppercase ASCII character 'A'-'Z' to the corresponding lowercase character.
/// @param ch The input character.
/// @return The lowercase version of the input character.
internal_function inline int
OsToLower
(
    char ch
)
{
    return (ch >= 'A' && ch <= 'Z') ? (ch + 32) : ch;
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

/// @summary Calculate the byte offset of the start of the next header in a tarball.
/// @param data_offset The absolute byte offset of the start of the data.
/// @param data_size The un-padded size of the file data, in bytes.
/// @return The byte offset of the start of the next header record.
internal_function int64_t 
OsTarNextHeaderOffset
(
    int64_t data_offset, 
    int64_t data_size
)
{   // assuming a block size of 512 bytes, which is typical.
    // round the data size up to the nearest even block multiple as
    // the data blocks are padded up to the block size with zero bytes.
    return data_offset + (((data_size + 511) / 512) * 512);
}

/// @summary Copy a string value until a zero-byte is encountered.
/// @param dst The start of the destination buffer.
/// @param src The start of the source buffer.
/// @param dst_offset The offset into the destination buffer at which to begin writing, in bytes.
/// @param max The maximum number of bytes to copy from the source to the destination.
/// @return The input dst_offset, plus the number of bytes copied from src to dst minus one.
internal_function size_t
OsTarStrcpy
(
    char         *dst, 
    char const   *src, 
    size_t dst_offset, 
    size_t        max
)
{
    size_t s = 0;
    size_t d = dst_offset;

    do {
        dst[d++] = src[s];
    } while (src[s] != 0 && s++ < max);

    return dst_offset + s;
}

/// @summary Parses a zero-padded octal string and converts it to a decimal value.
/// @param str The buffer containing the value to parse.
/// @param length The maximum number of bytes to read from the input string.
/// @return The decoded decimal integer value.
template <typename int_type>
internal_function  int_type 
OsTarOctalToDecimal
(
    char const *str, 
    size_t   length
)
{
    int_type    n = 0;
    for (size_t i = 0; i < length; ++i)
    {
        if (str[i] >= '0' && str[i] <= '8')
        {
            n *= 8;
            n += str[i] - 48;
        }
        else break;
    }
    return n;
}

/// @summary Parses an encoded TAR header entry to generate the data stored by the mount point.
/// @param dst The record to populate.
/// @param src The raw header record as it exists on disk.
/// @param offset The absolute byte offset of the start of the header record within the tarball.
/// @return The absolute byte offset of the start of the next header record.
internal_function int64_t 
OsTarDecodeEntry
(
    OS_TAR_ENTRY                *dst, 
    OS_TAR_HEADER_ENCODED const *src, 
    int64_t                   offset
)
{   // decode the base header fields.
    dst->FileSize    = OsTarOctalToDecimal< int64_t>(src->FileSize, 12);
    dst->FileTime    = OsTarOctalToDecimal<uint64_t>(src->FileTime, 12);
    dst->DataOffset  = offset + sizeof(OS_TAR_HEADER_ENCODED);
    dst->Checksum    = OsTarOctalToDecimal<uint32_t>(src->Checksum,  8);
    dst->FileType    = src->FileType;
    // check to see if a ustar extended header is present.
    OS_USTAR_HEADER_ENCODED const *ustar = (OS_USTAR_HEADER_ENCODED const*) src->ExtraPad;
    if (strncmp(ustar->MagicStr,  "ustar", 5) == 0)
    {   // a ustar extended header is present, which affects the path strings.
        size_t slen  = OsTarStrcpy(dst->FullPath, ustar->FileBase, 0, 155);
        // a single forward slash separates the base path and filename.
        if (slen > 0)  dst->FullPath[slen++] = '/';
        // append the filename to the path string.
        slen = OsTarStrcpy(dst->FullPath, src->FileName, slen, 100); dst->FullPath[slen] = 0;
        // the link name never has the BasePath prepended; copy it over as-is.
        memcpy(dst->LinkName, src->LinkName, 100 * sizeof(char)); dst->LinkName[100] = 0;
    }
    else
    {   // no extended header is present, so copy over the file and link name.
        memcpy(dst->FullPath, src->FileName, 100 * sizeof(char)); dst->FullPath[100] = 0;
        memcpy(dst->LinkName, src->LinkName, 100 * sizeof(char)); dst->LinkName[100] = 0;
    }
    return OsTarNextHeaderOffset(dst->DataOffset, dst->FileSize);
}

/// @summary Compare two mount point roots to determine if they match.
/// @param s1 A NULL-terminated UTF-8 string specifying the mount point identifier to match, with trailing slash '/'.
/// @param s2 A NULL-terminated UTF-8 string specifying the mount point identifier to check, with trailing slash '/'.
/// @return true if the mount point identifiers match.
internal_function bool
OsMountPointMatchExact
(
    char const *s1, 
    char const *s2
)
{   // use strcasecmp on Linux.
    return _stricmp(s1, s2) == 0;
}

/// @summary Determine whether a path references an entry under a given mount point.
/// @param mount A NULL-terminated UTF-8 string specifying the mount point root to match, with trailing slash '/'.
/// @param path A NULL-terminated UTF-8 string specifying the path to check.
/// @param len The number of bytes to compare from mount.
/// @return true if the path is under the mount point.
internal_function bool
OsMountPointMatchStart
(
    char   const *mount, 
    char   const  *path, 
    size_t          len
)
{   // use strncasecmp on Linux.
    if (len == 0) len = strlen(mount);
    return _strnicmp(mount, path, len - 1) == 0;
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

/// @summary Allocate storage for a mountpoint table.
/// @param table The OS_MOUNTPOINT_TABLE to initialize.
/// @param capcity The table capacity, in mount points.
/// @return Zero if the table is successfully initialized, or -1 if memory allocation failed.
internal_function int
OsCreateMountpointTable
(
    OS_MOUNTPOINT_TABLE *table, 
    size_t            capacity
)
{   // initialize all fields of the mount table instance.
    ZeroMemory(table, sizeof(OS_MOUNTPOINT_TABLE));

    // allocate storage for the specified number of items.
    table->Count               = 0;
    table->Capacity            = capacity;
    table->MountpointIds       =(uintptr_t    *) malloc(capacity * sizeof(uintptr_t));
    table->MountpointData      =(OS_MOUNTPOINT*) malloc(capacity * sizeof(OS_MOUNTPOINT));
    table->MountpointPriority  =(uint32_t     *) malloc(capacity * sizeof(uint32_t));
    if (table->MountpointIds  == NULL || table->MountpointData == NULL || table->MountpointPriority == NULL)
    {
        if (table->MountpointPriority != NULL) free(table->MountpointPriority);
        if (table->MountpointData != NULL) free(table->MountpointData);
        if (table->MountpointIds != NULL) free(table->MountpointIds);
        ZeroMemory(table, sizeof(OS_MOUNTPOINT_TABLE));
        return -1;
    }
    return 0;
}

/// @summary Update a mountpoint table with a new mount point.
/// @param table The OS_MOUNTPOINT_TABLE to update.
/// @param id The application-defined identifier of the new mount point.
/// @param priority The priority value of the mount point being added.
/// @return A pointer to the mount point data to populate, or NULL if the table is full.
internal_function OS_MOUNTPOINT*
OsAddMountpoint
(
    OS_MOUNTPOINT_TABLE *table, 
    uintptr_t               id, 
    uint32_t          priority
)
{
    intptr_t ins_idx  = 0;
    intptr_t min_idx  = 0;
    intptr_t max_idx  = 0;
    intptr_t end_idx  =(intptr_t) table->Count;

    if (table->Count == table->Capacity)
    {   // grow storage by a fixed-size amount.
        size_t     new_capacity = table->Capacity + 32;
        uintptr_t      *id_list =(uintptr_t    *) realloc(table->MountpointIds     , new_capacity * sizeof(uintptr_t));
        OS_MOUNTPOINT *rec_list =(OS_MOUNTPOINT*) realloc(table->MountpointData    , new_capacity * sizeof(OS_MOUNTPOINT));
        uint32_t      *pri_list =(uint32_t     *) realloc(table->MountpointPriority, new_capacity * sizeof(uint32_t));
        if (id_list  != NULL) table->MountpointIds      = id_list;
        if (rec_list != NULL) table->MountpointData     = rec_list;
        if (pri_list != NULL) table->MountpointPriority = pri_list;
        if (pri_list != NULL || rec_list != NULL || id_list != NULL)
        {   // all storage arrays were successfully grown. capacity was increased.
            table->Capacity = new_capacity;
        }
        else
        {   // unable to allocate storage for one or more arrays. capacity cannot be increased.
            return NULL;
        }
    }

    // perform a binary search to determine where the new item should be inserted.
    while (min_idx <= max_idx)
    {   // expect that many items will have the same priority value.
        intptr_t mid  =(min_idx + max_idx) / 2;
        uint32_t pri  = table->MountpointPriority[mid];
        if (priority == pri)
        {   // find the end of this run so that the new item appears last.
            ins_idx   = mid;
            while (priority == table->MountpointPriority[ins_idx])
            {
                ins_idx++;
            }
            break;
        }
        else if (priority > pri)
        {   // continue searching the lower half of the range.
            max_idx = mid - 1;
        }
        else
        {   // continue searching the upper half of the range.
            min_idx = mid + 1;
        }
    }
    // insert the new item into the mount point table at the designated location.
    if (ins_idx != end_idx)
    {   // shift the other list items down by one.
        intptr_t dst = end_idx;
        intptr_t src = end_idx - 1;
        do {
            table->MountpointIds     [dst] = table->MountpointIds     [src];
            table->MountpointData    [dst] = table->MountpointData    [src];
            table->MountpointPriority[dst] = table->MountpointPriority[src];
            dst  = src--;
        } while (src >= ins_idx);
        // insert the new item into the list.
        table->MountpointIds[ins_idx] = id;
        table->MountpointPriority[ins_idx] = priority;
        table->Count++;
    }
    else
    {   // insert the item at the end of the list.
        ins_idx = end_idx++;
        table->MountpointIds[ins_idx] = id;
        table->MountpointPriority[ins_idx] = priority;
        table->Count++;
    }
    return &table->MountpointData[ins_idx];
}

/// @summary Remove a mountpoint at a given location within the mount point table.
/// @param table The OS_MOUNTPOINT_TABLE to update.
/// @param index The zero-based index of the item to remove.
internal_function void
OsRemoveMountpointAt
(
    OS_MOUNTPOINT_TABLE *table, 
    size_t               index
)
{   assert(index <= table->Count && table->Count > 0);
    if (table->Count > 0)
    {   
        size_t dst = index;
        size_t src = index + 1;

        // release resources associated with the item being unmounted.
        table->MountpointData[index].Unmount(&table->MountpointData[index]);
        free(table->MountpointData[index].Root);
        free(table->MountpointData[index].LocalPath);
        ZeroMemory(&table->MountpointData[index], sizeof(OS_MOUNTPOINT));

        // shift all of the other items up on top of the removed item.
        for (size_t i = index, n = table->Count - 1; i < n; ++i, ++dst, ++src)
        {
            table->MountpointIds     [dst] = table->MountpointIds     [src];
            table->MountpointData    [dst] = table->MountpointData    [src];
            table->MountpointPriority[dst] = table->MountpointPriority[src];
        }
        table->Count--;
    }
}

/// @summary Removes a mount point with a given ID from a prioritized mount list.
/// @param table The OS_MOUNTPOINT_TABLE to update.
/// @param id The unique application identifier of the mount point to remove.
internal_function void
OsRemoveMountpointId
(
    OS_MOUNTPOINT_TABLE *table, 
    uintptr_t               id
)
{
    for (size_t i = 0, n = table->Count; i < n; ++i)
    {
        if (table->MountpointIds[i] == id)
        {
            OsRemoveMountpointAt(table, i);
            break;
        }
    }
}

/// @summary Synchronously open a file for access. The information necessary to access the file is written to @a file.
/// @param mount The mount point performing the file open operation.
/// @param path The zero-terminated UTF-8 path of the file to open, relative to the mount point root.
/// @param usage One of OS_FILE_USAGE specifying the intended use of the file by the application.
/// @param hints One or more of OS_FILE_USAGE_HINTS specifying additional desired behaviors.
/// @param file If the call returns OS_IO_RESULT_SUCCESS, this structure should be populated with file data.
/// @return One of OS_IO_RESULT indicating the result of the operation.
internal_function int 
OsOpenFile_Filesystem
(
    OS_MOUNTPOINT *mount, 
    char const     *path, 
    int            usage, 
    uint32_t       hints, 
    OS_FILE        *file
)
{
    LARGE_INTEGER fsize = {0};
    WCHAR      *pathbuf = OsMakeSystemPathMalloc(mount->LocalPath, mount->LocalPathLength, path);
    HANDLE        hFile = INVALID_HANDLE_VALUE;
    HANDLE     hFileMap = NULL;
    DWORD        access = 0;
    DWORD         share = 0;
    DWORD        create = 0;
    DWORD         flags = 0;
    DWORD       protect = 0;
    size_t        ssize = 0;

    // initialize the fields of the file object.
    ZeroMemory(file, sizeof(OS_FILE));

    // figure out access flags, share modes, etc. based on the intended usage.
    switch (usage)
    {
    case OS_FILE_USAGE_STREAM_IN:
        {
            access = GENERIC_READ;
            share  = FILE_SHARE_READ;
            create = OPEN_EXISTING;
            flags  = FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED;
            if (hints & OS_FILE_USAGE_HINT_UNBUFFERED)
            {
                flags |= FILE_FLAG_NO_BUFFERING;
            }
        } break;
    case OS_FILE_USAGE_STREAM_OUT:
        {
            access = GENERIC_READ | GENERIC_WRITE;
            share  = FILE_SHARE_READ;
            create = CREATE_ALWAYS;
            flags  = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED;
            if (hints & OS_FILE_USAGE_HINT_UNBUFFERED)
            {
                flags |= FILE_FLAG_NO_BUFFERING;
            }
        } break;
    case OS_FILE_USAGE_MMAP_READ:
        {
            access  = GENERIC_READ;
            share   = FILE_SHARE_READ;
            create  = OPEN_EXISTING;
            flags   = FILE_FLAG_SEQUENTIAL_SCAN;
            protect = PAGE_READONLY;
        } break;
    case OS_FILE_USAGE_MMAP_WRITE:
        {
            access  = GENERIC_READ | GENERIC_WRITE;
            share   = 0;
            create  = OPEN_ALWAYS;
            flags   = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
            protect = PAGE_READWRITE;
        } break;
    case OS_FILE_USAGE_MANUAL_IO:
        {
            access = GENERIC_READ | GENERIC_WRITE;
            share  = FILE_SHARE_READ;
            create = OPEN_ALWAYS;
            flags  = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
            if (hints & OS_FILE_USAGE_HINT_UNBUFFERED)
            {
                flags |= FILE_FLAG_NO_BUFFERING;
            }
            if (hints & OS_FILE_USAGE_HINT_ASYNCHRONOUS)
            {
                flags |= FILE_FLAG_OVERLAPPED;
            }
            if (hints & OS_FILE_USAGE_HINT_OVERWRITE)
            {
                create = CREATE_ALWAYS;
            }
        } break;
    default:
        return OS_IO_RESULT_NOT_SUPPORTED;
    }

    // open the file as specified by the caller.
    if ((hFile = CreateFileW(pathbuf, access, share, NULL, create, flags, NULL)) == INVALID_HANDLE_VALUE)
    {   // the file cannot be opened.
        OsLayerError("ERROR: %S(%u): Unable to open file %s (user path %S) (%08X).\n", __FUNCTION__, GetCurrentThreadId(), pathbuf, path, GetLastError());
        goto cleanup_and_fail;
    }

    // retrieve basic file attributes.
    ssize = OsPhysicalSectorSize(hFile);
    if (!GetFileSizeEx(hFile, &fsize))
    {   // the file size cannot be retrieved.
        OsLayerError("ERROR: %S(%u): Unable to retrieve size for file %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), pathbuf, GetLastError());
        goto cleanup_and_fail;
    }

    // clean up temporary buffers.
    free(pathbuf); pathbuf = NULL;

    // if the file is being opened for memory-mapped I/O, create the mapping.
    if (usage == OS_FILE_USAGE_MMAP_READ || usage == OS_FILE_USAGE_MMAP_WRITE)
    {   // create the file mapping over the entire file.
        if ((hFileMap = CreateFileMapping(hFile, NULL, protect, 0, 0, NULL)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to create file mapping for file %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), pathbuf, GetLastError());
            goto cleanup_and_fail;
        }
    }

    // set output parameters and clean up.
    file->Filedes    = hFile;
    file->Filemap    = hFileMap;
    file->SectorSize = ssize;
    file->BaseOffset = 0;
    file->BaseSize   = fsize.QuadPart;
    file->FileSize   = fsize.QuadPart;
    file->FileHints  = hints;
    file->FileFlags  = 0;
    file->OSError    = ERROR_SUCCESS;
    file->OpenFlags  = flags;
    return OS_IO_RESULT_SUCCESS;

cleanup_and_fail:
    if (hFileMap != NULL) CloseHandle(hFileMap);
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    if (pathbuf != NULL) free(pathbuf);
    ZeroMemory(file, sizeof(OS_FILE));
    file->OpenFlags  = flags;
    file->Filedes    = INVALID_HANDLE_VALUE;
    file->Filemap    = NULL;
    file->FileHints  = hints;
    file->OSError    = GetLastError();
    return OS_IO_RESULT_OSERROR;
}

/// @summary Synchronously write an entire file to disk. If the file exists, its contents are overwritten; otherwise, a new file is created.
/// @param mount The mount point performing the file save operation.
/// @param path The zero-terminated UTF-8 path of the file to write, relative to the mount point root.
/// @param data The buffer containing the data to write to the file.
/// @param file_size The number of bytes to read from the data buffer and write to the file.
/// @return One of OS_IO_RESULT indicating the result of the operation.
internal_function int
OsSaveFile_Filesystem
(
    OS_MOUNTPOINT *mount, 
    char    const  *path, 
    void    const  *data, 
    int64_t const   size
)
{
    FILE_END_OF_FILE_INFO eof;
    FILE_ALLOCATION_INFO  sec;
    uint8_t const     *buffer = (uint8_t const  *) data;
    SYSTEM_INFO       sysinfo = {0};
    HANDLE                 fd = INVALID_HANDLE_VALUE;
    DWORD           mem_flags = MEM_RESERVE  | MEM_COMMIT;
    DWORD         mem_protect = PAGE_READWRITE;
    DWORD              access = GENERIC_READ | GENERIC_WRITE;
    DWORD               share = FILE_SHARE_READ;
    DWORD              create = CREATE_ALWAYS;
    DWORD               flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING;
    WCHAR          *file_path = NULL;
    size_t      file_path_len = 0;
    WCHAR          *temp_path = NULL;
    WCHAR           *temp_ext = NULL;
    size_t       temp_ext_len = 0;
    DWORD           page_size = 4096;
    int64_t         file_size = 0;
    uint8_t    *sector_buffer = NULL;
    size_t       sector_count = 0;
    int64_t      sector_bytes = 0;
    size_t        sector_over = 0;
    size_t        sector_size = 0;

    // get the system page size, and allocate a single-page buffer. to support
    // unbuffered I/O, the buffer must be allocated on a sector-size boundary
    // and must also be a multiple of the physical disk sector size. allocating
    // a virtual memory page using VirtualAlloc will satisfy these constraints.
    GetNativeSystemInfo(&sysinfo); page_size = sysinfo.dwPageSize;
    if ((sector_buffer = (uint8_t*) VirtualAlloc(NULL, page_size, mem_flags, mem_protect)) == NULL)
    {   // unable to allocate the overage buffer; check GetLastError().
        OsLayerError("ERROR: %S(%u): Unable to allocate overage buffer for file %S (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
        goto cleanup_and_fail;
    }

    // generate a temporary filename in the same location as the output file.
    // this prevents the file from being copied when it is moved, which happens
    // if the temp file isn't on the same volume as the output file.
    if ((file_path = OsMakeSystemPathMalloc(mount->LocalPath, mount->LocalPathLength, path)) == NULL)
    {   // unable to allocate the output path buffer.
        OsLayerError("ERROR: %S(%u): Unable to allocate system path buffer for file %S.\n", __FUNCTION__, GetCurrentThreadId(), path);
        goto cleanup_and_fail;
    }
    temp_ext_len   =  4; // wcslen(L".tmp")
    file_path_len  =  wcslen(file_path);
    if ((temp_path = (WCHAR*) malloc((file_path_len + temp_ext_len + 1) * sizeof(WCHAR))) == NULL)
    {   // unable to allocate temporary path memory.
        OsLayerError("ERROR: %S(%u): Unable to allocate temporary path buffer for file %s (%S).\n", __FUNCTION__, GetCurrentThreadId(), file_path, path);
        goto cleanup_and_fail;
    }
    temp_ext  = temp_path + file_path_len;
    memcpy(temp_path, file_path, file_path_len   * sizeof(WCHAR));
    memcpy(temp_ext , L".tmp"  ,(temp_ext_len+1) * sizeof(WCHAR));

    // create the temporary file, and get the physical disk sector size.
    // pre-allocate storage for the file contents, which should improve performance.
    if ((fd = CreateFile(temp_path, access, share, NULL, create, flags, NULL)) == INVALID_HANDLE_VALUE)
    {   // unable to create the new file; check GetLastError().
        OsLayerError("ERROR: %S(%u): Unable to create staging file %s for file %S (%08X).\n", __FUNCTION__, GetCurrentThreadId(), temp_path, path, GetLastError());
        goto cleanup_and_fail;
    }
    sector_size = OsPhysicalSectorSize(fd);
    file_size   = OsAlignUp(size, sector_size);
    sec.AllocationSize.QuadPart = file_size;
    SetFileInformationByHandle(fd, FileAllocationInfo , &sec, sizeof(sec));

    // copy the data extending into the tail sector into the overage buffer.
    sector_count = size_t (size / sector_size);
    sector_bytes = int64_t(sector_size) * sector_count;
    sector_over  = size_t (size - sector_bytes);
    if (sector_over > 0)
    {   // buffer the overlap amount. note that the page will have been zeroed.
        memcpy(sector_buffer, &buffer[sector_bytes], sector_over);
    }

    // write the data to the file.
    if (sector_bytes > 0)
    {   // write the bulk of the data, if the data is > 1 sector.
        int64_t amount = 0;
        DWORD   nwrite = 0;
        while  (amount < sector_bytes)
        {
            DWORD n = (sector_bytes - amount) < 0xFFFFFFFFU ? DWORD(sector_bytes - amount) : 0xFFFFFFFFU;
            if (!WriteFile(fd, &buffer[amount], n, &nwrite, NULL))
            {
                OsLayerError("ERROR: %S(%u): Failed write to staging file %s for file %S (%08X).\n", __FUNCTION__, GetCurrentThreadId(), temp_path, path, GetLastError());
                goto cleanup_and_fail;
            }
            amount += nwrite;
        }
    }
    if (sector_over > 0)
    {   // write the remaining sector-sized chunk of data.
        DWORD n = (DWORD) sector_size;
        DWORD w = (DWORD) 0;
        if (!WriteFile(fd, sector_buffer, n, &w, NULL))
        {
            OsLayerError("ERROR: %S(%u): Failed last-sector write to staging file %s for file %S (%08X).\n", __FUNCTION__, GetCurrentThreadId(), temp_path, path, GetLastError());
            goto cleanup_and_fail;
        }
    }

    // set the correct end-of-file marker.
    eof.EndOfFile.QuadPart = size;
    SetFileInformationByHandle(fd, FileEndOfFileInfo, &eof, sizeof(eof));
    SetFileValidData(fd, eof.EndOfFile.QuadPart);

    // close the file, and move it to the destination path.
    CloseHandle(fd); fd = INVALID_HANDLE_VALUE;
    if (!MoveFileEx(temp_path, file_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {   // the file could not be moved into place; check GetLastError().
        OsLayerError("ERROR: %S(%u): Unable to move staging file %s to %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), temp_path, file_path, GetLastError());
        goto cleanup_and_fail;
    }

    // clean up our temporary buffers; we're done.
    free(temp_path); temp_path =  NULL;
    free(file_path); file_path =  NULL;
    VirtualFree(sector_buffer, 0, MEM_RELEASE);
    return OS_IO_RESULT_SUCCESS;

cleanup_and_fail:
    if (fd != INVALID_HANDLE_VALUE) CloseHandle(fd);
    if (temp_path     != NULL)      DeleteFile(temp_path);
    if (temp_path     != NULL)      free(temp_path);
    if (file_path     != NULL)      free(file_path);
    if (sector_buffer != NULL)      VirtualFree(sector_buffer, 0, MEM_RELEASE);
    return OS_IO_RESULT_OSERROR;
}

/// @summary Determine whether a given mount point supports a particular type of file access.
/// @param mount The mount point to query.
/// @param usage One of OS_FILE_USAGE specifying the intended use of the file by the application.
/// @return OS_IO_RESULT_SUCCESS if the usage is supported, or OS_IO_RESULT_NOT_SUPPORTED.
internal_function int
OsSupportsUsage_Filesystem
(
    OS_MOUNTPOINT *mount, 
    int            usage
)
{
    switch (usage)
    {
        case OS_FILE_USAGE_STREAM_IN : return OS_IO_RESULT_SUCCESS;
        case OS_FILE_USAGE_STREAM_OUT: return OS_IO_RESULT_SUCCESS;
        case OS_FILE_USAGE_MMAP_READ : return OS_IO_RESULT_SUCCESS;
        case OS_FILE_USAGE_MMAP_WRITE: return OS_IO_RESULT_SUCCESS;
        case OS_FILE_USAGE_MANUAL_IO : return OS_IO_RESULT_SUCCESS;
        default: return OS_IO_RESULT_NOT_SUPPORTED;
    }
    UNREFERENCED_PARAMETER(mount);
}

/// @summary Define the signature for the callback invoked when a mount point is being removed.
/// @param mount The mount point being unmounted.
internal_function void
OsUnmount_Filesystem
(
    OS_MOUNTPOINT *mount
)
{
    UNREFERENCED_PARAMETER(mount);
}

/// @summary Initialize a filesystem-based mountpoint and allocate the private state data.
/// @param mount The OS_VFS_MOUNT instance to initialize.
/// @param local_path The NULL-terminated string specifying the local path that functions as the root of the mount point. This directory must exist.
/// @return true if the mount point was initialized successfully.
internal_function int
OsCreateMount_Filesystem
(
    OS_MOUNTPOINT    *mount, 
    WCHAR const *local_path
)
{
    size_t  pathlen = 0;
    WCHAR  *pathbuf = NULL;
    DWORD     share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE     hDir = INVALID_HANDLE_VALUE;

    // to get a consistent, fully-resolved path to use as the base path, 
    // open the directory, and then retrieve the path from the resulting handle.
    if ((hDir = CreateFile(local_path, 0, share, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL)) == INVALID_HANDLE_VALUE)
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize filesystem mount point %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), local_path, GetLastError());
        goto cleanup_and_fail;
    }
    if ((pathbuf = OsResolvePathForHandleMalloc(hDir, pathlen)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve path for filesystem mount point %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), local_path, GetLastError());
        goto cleanup_and_fail;
    }
    CloseHandle(hDir);

    // everything was successful; set up the mount point fields.
    // the Id, Root and RootLength fields are set up by the caller.
    mount->LocalPath       = pathbuf;
    mount->LocalPathLength = pathlen;
    mount->StateData       = NULL;
    mount->OpenFile        = OsOpenFile_Filesystem;
    mount->SaveFile        = OsSaveFile_Filesystem;
    mount->SupportsUsage   = OsSupportsUsage_Filesystem;
    mount->Unmount         = OsUnmount_Filesystem;
    return 0;

cleanup_and_fail:
    if (hDir != INVALID_HANDLE_VALUE) CloseHandle(hDir);
    if (pathbuf != NULL) free(pathbuf);
    return -1;
}

/// @summary Synchronously open a file for access. The information necessary to access the file is written to @a file.
/// @param mount The mount point performing the file open operation.
/// @param path The zero-terminated UTF-8 path of the file to open, relative to the mount point root.
/// @param usage One of OS_FILE_USAGE specifying the intended use of the file by the application.
/// @param hints One or more of OS_FILE_USAGE_HINTS specifying additional desired behaviors.
/// @param file If the call returns OS_IO_RESULT_SUCCESS, this structure should be populated with file data.
/// @return One of OS_IO_RESULT indicating the result of the operation.
internal_function int 
OsOpenFile_Tarball
(
    OS_MOUNTPOINT *mount, 
    char const     *path, 
    int            usage, 
    uint32_t       hints, 
    OS_FILE        *file
)
{
    OS_MOUNTPOINT_TAR    *tar =(OS_MOUNTPOINT_TAR*) mount->StateData;
    uint32_t const *hash_list = tar->TarballData.EntryHash;
    HANDLE              hFile = INVALID_HANDLE_VALUE;
    HANDLE           hFileMap = NULL;
    DWORD              access = 0;
    DWORD               share = 0;
    DWORD              create = 0;
    DWORD               flags = 0;
    DWORD             protect = 0;
    uint32_t             hash = OsHashPath(path);

    // initialize the fields of the file object.
    ZeroMemory(file, sizeof(OS_FILE));

    // figure out access flags, share modes, etc. based on the intended usage.
    switch (usage)
    {
    case OS_FILE_USAGE_STREAM_IN:
        {
            access = GENERIC_READ;
            share  = FILE_SHARE_READ;
            create = OPEN_EXISTING;
            flags  = FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED;
            // alignment restrictions can't be guaranteed, so unbuffered I/O isn't supported.
        } break;
    case OS_FILE_USAGE_STREAM_OUT:
        { // stream out is not supported.
        } return OS_IO_RESULT_NOT_SUPPORTED;
    case OS_FILE_USAGE_MMAP_READ:
        {
            access  = GENERIC_READ;
            share   = FILE_SHARE_READ;
            create  = OPEN_EXISTING;
            flags   = FILE_FLAG_SEQUENTIAL_SCAN;
            protect = PAGE_READONLY;
        } break;
    case OS_FILE_USAGE_MMAP_WRITE:
        { // mmap-for-write is not supported.
        } return OS_IO_RESULT_NOT_SUPPORTED;
    case OS_FILE_USAGE_MANUAL_IO:
        { // manual I/O is not supported (writing is not supported.)
        } return OS_IO_RESULT_NOT_SUPPORTED;
    default:
        return OS_IO_RESULT_NOT_SUPPORTED;
    }

    // locate the file within the tarball by comparing hashes.
    for (size_t i = 0, n = tar->TarballData.EntryCount; i < n; ++i)
    {
        if (hash_list[i] == hash)
        {   // hashes match, confirm by comparing strings.
            if (_stricmp(path, tar->TarballData.EntryInfo[i].FullPath) == 0)
            {   // found an exact match for the path; duplicate the file handle(s) for the caller.
                HANDLE p = GetCurrentProcess();
                if (!DuplicateHandle(p, tar->TarFiledes, p, &hFile, 0, FALSE, DUPLICATE_SAME_ACCESS))
                {
                    OsLayerError("ERROR: %S(%u): Unable to duplicate TAR file handle for file %S (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
                    goto cleanup_and_fail;
                }
                if (usage == OS_FILE_USAGE_MMAP_READ)
                {   // also create a new read-only file mapping object.
                    if ((hFileMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL)) == NULL)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to create file mapping object for file %S (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
                        goto cleanup_and_fail;
                    }
                }
                file->Filedes    = hFile;
                file->Filemap    = hFileMap;
                file->SectorSize = tar->SectorSize;
                file->BaseOffset = tar->TarballData.EntryInfo[i].DataOffset;
                file->BaseSize   = tar->TarballData.EntryInfo[i].FileSize;
                file->FileSize   = tar->TarballData.EntryInfo[i].FileSize;
                file->FileHints  = hints;
                file->FileFlags  = 0;
                file->OSError    = ERROR_SUCCESS;
                file->OpenFlags  = flags;
                return OS_IO_RESULT_SUCCESS;
            }
        }
    }

    // else, the file was not found in the tarball.
    // fall-through to cleanup_and_fail.
    SetLastError(ERROR_FILE_NOT_FOUND);

cleanup_and_fail:
    if (hFileMap != NULL) CloseHandle(hFileMap);
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    ZeroMemory(file, sizeof(OS_FILE));
    file->OpenFlags  = flags;
    file->Filedes    = INVALID_HANDLE_VALUE;
    file->Filemap    = NULL;
    file->FileHints  = hints;
    file->OSError    = GetLastError();
    return OS_IO_RESULT_OSERROR;
}

/// @summary Synchronously write an entire file to disk. If the file exists, its contents are overwritten; otherwise, a new file is created.
/// @param mount The mount point performing the file save operation.
/// @param path The zero-terminated UTF-8 path of the file to write, relative to the mount point root.
/// @param data The buffer containing the data to write to the file.
/// @param file_size The number of bytes to read from the data buffer and write to the file.
/// @return One of OS_IO_RESULT indicating the result of the operation.
internal_function int
OsSaveFile_Tarball
(
    OS_MOUNTPOINT *mount, 
    char    const  *path, 
    void    const  *data, 
    int64_t const   size
)
{
    UNREFERENCED_PARAMETER(mount);
    UNREFERENCED_PARAMETER(path);
    UNREFERENCED_PARAMETER(data);
    UNREFERENCED_PARAMETER(size);
    return OS_IO_RESULT_NOT_SUPPORTED;
}

/// @summary Determine whether a given mount point supports a particular type of file access.
/// @param mount The mount point to query.
/// @param usage One of OS_FILE_USAGE specifying the intended use of the file by the application.
/// @return OS_IO_RESULT_SUCCESS if the usage is supported, or OS_IO_RESULT_NOT_SUPPORTED.
internal_function int
OsSupportsUsage_Tarball
(
    OS_MOUNTPOINT *mount, 
    int            usage
)
{
    switch (usage)
    {
        case OS_FILE_USAGE_STREAM_IN : return OS_IO_RESULT_SUCCESS;
        case OS_FILE_USAGE_STREAM_OUT: return OS_IO_RESULT_NOT_SUPPORTED;
        case OS_FILE_USAGE_MMAP_READ : return OS_IO_RESULT_SUCCESS;
        case OS_FILE_USAGE_MMAP_WRITE: return OS_IO_RESULT_NOT_SUPPORTED;
        case OS_FILE_USAGE_MANUAL_IO : return OS_IO_RESULT_NOT_SUPPORTED;
        default: return OS_IO_RESULT_NOT_SUPPORTED;
    }
    UNREFERENCED_PARAMETER(mount);
}

/// @summary Define the signature for the callback invoked when a mount point is being removed.
/// @param mount The mount point being unmounted.
internal_function void
OsUnmount_Tarball
(
    OS_MOUNTPOINT *mount
)
{
    if (mount != NULL)
    {   // free the internal state data.
        if (mount->StateData != NULL)
        {
            OS_MOUNTPOINT_TAR *tar = (OS_MOUNTPOINT_TAR*) mount->StateData;
            OsUnloadTarball(&tar->TarballData);
            CloseHandle(tar->TarFiledes);
        }
        free(mount->StateData);
    }
}

/// @summary Initialize a tarball-based mountpoint and allocate the private state data.
/// @param mount The OS_MOUNTPOINT instance to initialize.
/// @param local_path The NULL-terminated string specifying the local path that functions as the root of the mount point. This directory must exist.
/// @return Zero if the mount point was initialized successfully, or -1 if an error occurred.
internal_function int
OsCreateMount_Tarball
(
    OS_MOUNTPOINT    *mount, 
    WCHAR const *local_path
)
{
    OS_MOUNTPOINT_TAR *tar = NULL;
    WCHAR         *pathbuf = NULL;
    size_t         pathlen = 0;
    DWORD           access = GENERIC_READ;
    DWORD           create = OPEN_EXISTING;
    DWORD            share = FILE_SHARE_READ;
    HANDLE            hTar = INVALID_HANDLE_VALUE;
    DWORD           result = ERROR_SUCCESS;

    // to get a consistent, fully-resolved path to use as the base path, 
    // open the file, and then retrieve the path from the resulting handle.
    if ((hTar = CreateFile(local_path, access, share, NULL, create, FILE_FLAG_SEQUENTIAL_SCAN, NULL)) == INVALID_HANDLE_VALUE)
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize tarball mount point %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), local_path, GetLastError());
        goto cleanup_and_fail;
    }
    if ((pathbuf = OsResolvePathForHandleMalloc(hTar, pathlen)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve path name for tarball mount point %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), local_path, GetLastError());
        goto cleanup_and_fail;
    }
    if ((tar = (OS_MOUNTPOINT_TAR*) malloc(sizeof(OS_MOUNTPOINT_TAR))) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate memory for tarball mount point %s.\n", __FUNCTION__, GetCurrentThreadId(), local_path);
        goto cleanup_and_fail;
    }
    if (OsLoadTarball(&tar->TarballData, hTar, result) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to parse tarball for mount point %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), local_path, result);
        goto cleanup_and_fail;
    }

    // everything was successful; set up the mount point fields.
    // the Id, Root and RootLength fields are set up by the caller.
    tar->TarFiledes        = hTar;
    tar->SectorSize        = OsPhysicalSectorSize(hTar);
    mount->LocalPath       = pathbuf;
    mount->LocalPathLength = pathlen;
    mount->StateData       = tar;
    mount->OpenFile        = OsOpenFile_Tarball;
    mount->SaveFile        = OsSaveFile_Tarball;
    mount->SupportsUsage   = OsSupportsUsage_Tarball;
    mount->Unmount         = OsUnmount_Tarball;
    return 0;

cleanup_and_fail:
    if (hTar != INVALID_HANDLE_VALUE) CloseHandle(hTar);
    OsUnloadTarball(&tar->TarballData);
    if (pathbuf != NULL) free(pathbuf);
    if (tar != NULL) free(tar);
    return -1;
}

/// @summary Performs all of the common setup when creating a new mount point.
/// @param file_system The file system the mount point is attached to.
/// @param source_wide The NULL-terminated source directory or file path.
/// @param source_attr The attributes of the source path, as returned by GetFileAttributes().
/// @param mount_path The NULL-terminated UTF-8 string specifying the mount point root exposed externally.
/// @param priority The priority of the mount point. Higher numeric values indicate higher priority.
/// @param mount_id An application-defined unique identifier for the mount point. This is necessary if the application later wants to remove this specific mount point, but not any of the others that are mounted with the same mount_path.
/// @return Zero if the mount point was successfully installed, or -1 if an error occurred.
internal_function int 
OsSetupMountpoint
(
    OS_FILE_SYSTEM *file_system, 
    WCHAR          *source_wide, 
    DWORD           source_attr, 
    char const      *mount_path, 
    uint32_t           priority, 
    uintptr_t          mount_id
)
{   
    OS_MOUNTPOINT *mount = NULL;
    char     *mount_root = NULL;
    size_t   mount_bytes = strlen(mount_path);
    size_t  source_chars = wcslen(source_wide);
    
    // create the mount point record in the prioritized list.
    AcquireSRWLockExclusive(&file_system->MountpointLock);
    if ((mount = OsAddMountpoint(&file_system->MountpointTable, mount_id, priority)) == NULL)
    {   // most likely, the mountpoint table is full.
        OsLayerError("ERROR: %S(%u): Unable to insert mountpoint %S (%Iu/%Iu mountpoints used.)\n", __FUNCTION__, GetCurrentThreadId(), mount_path, file_system->MountpointTable.Count, file_system->MountpointTable.Capacity);
        goto cleanup_and_fail;
    }

    // initialize the basic mount point properties:
    mount->Id              = mount_id;
    mount->Root            = NULL;
    mount->RootLength      = mount_bytes;
    mount->LocalPath       = NULL;
    mount->LocalPathLength = 0;

    // create version of mount_path with trailing slash.
    if (mount_bytes && mount_path[mount_bytes-1] != '/')
    {   // it's necessary to append the trailing slash.
        if ((mount_root = (char*) malloc(mount_bytes  + 2)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate memory for mount point root path string.\n", __FUNCTION__, GetCurrentThreadId());
            goto cleanup_and_fail;
        }
        memcpy(mount_root, mount_path, mount_bytes);
        mount_root[mount_bytes+0] = '/';
        mount_root[mount_bytes+1] =  0 ;
        mount->Root = mount_root;
    }
    else
    {   // the mount path has the trailing slash; copy the string.
        if ((mount_root = (char*) malloc(mount_bytes  + 1)) == NULL)
        {
            OsLayerError("ERROR: %S(%u): Unable to allocate memory for mount point root path string.\n", __FUNCTION__, GetCurrentThreadId());
            goto cleanup_and_fail;
        }
        memcpy(mount_root, mount_path, mount_bytes);
        mount_root[mount_bytes] = 0;
        mount->Root = mount_root;
    }

    // initialize the mount point internals based on the source type.
    if (source_attr & FILE_ATTRIBUTE_DIRECTORY)
    {   // set up a filesystem-based mount point.
        if (OsCreateMount_Filesystem(mount, source_wide) < 0)
        {   // OsVfsCreateMount_Filesystem outputs its own errors.
            goto cleanup_and_fail;
        }
        ReleaseSRWLockExclusive(&file_system->MountpointLock);
        return 0;
    }
    else
    {   // this is an archive file. extract the file extension.
        WCHAR *ext = source_wide + source_chars;
        while (ext > source_wide)
        {   // search for the final period character.
            if (*ext == L'.')
            {   // ext points to the first character of the extension.
                ext++;
                break;
            }
            else ext--;
        }
        // now a bunch of if (wcsicmp(ext, L"zip") { ... } etc.
        if (!_wcsicmp(ext, L"tar"))
        {   // set up a tarball-based mount point.
            if (OsCreateMount_Tarball(mount, source_wide) < 0)
            {   // OsVfsCreateMount_Tarball outputs its own errors.
                goto cleanup_and_fail;
            }
            ReleaseSRWLockExclusive(&file_system->MountpointLock);
            return 0;
        }
        // ...
        else
        {
            OsLayerError("ERROR: %S(%u): Mountpoint extension %s has no associated driver.\n", __FUNCTION__, GetCurrentThreadId(), ext);
            goto cleanup_and_fail;
        }
    }

cleanup_and_fail:
    if (mount != NULL) OsRemoveMountpointId(&file_system->MountpointTable, mount_id);
    ReleaseSRWLockExclusive(&file_system->MountpointLock);
    if (mount_root) free(mount_root);
    return -1;
}

/// @summary Resolves a virtual path to a filesystem mount point, and then converts the path to an absolute path on the filesystem. Non-filesystem mount points are skipped.
/// @param file_system The file system to search.
/// @param path The virtual path to translate to a filesystem path.
/// @param pathbuf The buffer that will receive the filesystem path.
/// @param buflen The maximum number of wide characters to write to pathbuf.
/// @return Zero if the path was successfully resolved, or -1 if an error occurred.
internal_function int 
OsResolveFilesystemPath
(
    OS_FILE_SYSTEM *file_system, 
    char const            *path, 
    WCHAR              *pathbuf, 
    size_t               buflen
)
{   // iterate over the mount points, which are stored in priority order.
    AcquireSRWLockShared(&file_system->MountpointLock);
    OS_MOUNTPOINT *mounts = file_system->MountpointTable.MountpointData;
    for (size_t i = 0,  n = file_system->MountpointTable.Count; i < n; ++i)
    {
        OS_MOUNTPOINT *m  = &mounts[i];
        if (OsMountPointMatchStart(m->Root, path, m->RootLength) && m->OpenFile == OsOpenFile_Filesystem)
        {   // resolve the absolute path of the file on the filesystem.
            char const *relative_path = path + m->RootLength + 1;
            size_t             offset = m->LocalPathLength;
            int                nchars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, relative_path, -1, NULL, 0);
            if (nchars == 0 || buflen < (nchars + offset + 1))
            {   // the path cannot be converted from UTF-8 to UCS-2.
                OsLayerError("ERROR: %S(%u): Unable to retrieve UCS2 path length for file %S (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
                ReleaseSRWLockShared(&file_system->MountpointLock);
                return -1;
            }
            // start with the local root, and then append the relative path portion.
            memcpy(pathbuf, m->LocalPath, m->LocalPathLength * sizeof(WCHAR));
            pathbuf[m->LocalPathLength] = L'\\';
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, relative_path, -1, &pathbuf[m->LocalPathLength+1], nchars) == 0)
            {   // the path cannot be converted from UTF-8 to UCS-2.
                OsLayerError("ERROR: %S(%u): Unable to convert UTF8 path to UCS2 for file %S (%08X).\n", __FUNCTION__, GetCurrentThreadId(), path, GetLastError());
                ReleaseSRWLockShared(&file_system->MountpointLock);
                return -1;
            }
            ReleaseSRWLockShared(&file_system->MountpointLock);
            // normalize the directory separator characters to the system default.
            // on Windows, this is not strictly necessary, but on other OS's it is.
            for (size_t ch = offset + 1, nch = offset + nchars; ch < nch; ++ch)
            {
                if (pathbuf[ch] == L'/')
                    pathbuf[ch]  = L'\\';
            }
            return 0;
        }
    }
    OsLayerError("ERROR: %S(%u): Unable to find filesystem mountpoint for file %S.\n", __FUNCTION__, GetCurrentThreadId(), path);
    ReleaseSRWLockShared(&file_system->MountpointLock);
    return -1;
}

/// @summary Opens a file for access. Information necessary to access the file is written to the OS_FILE structure. This function tests each mount point matching the path, in priority order, until one is successful at opening the file or all matching mount points have been tested.
/// @param file_system The file system interface to query.
/// @param path The absolute virtual path of the file to open.
/// @param usage One of OS_FILE_USAGE specifying the intended use of the file.
/// @param hints One or more of OS_FILE_USAGE_HINTS.
/// @param file On return, this structure should be populated with the data necessary to access the file. If the file cannot be opened, check the OSError field.
/// @param relative_path On return, this pointer is updated to point into path, at the start of the portion of the path representing the path to the file, relative to the mount point root path.
/// @return One of OS_IO_RESULT.
internal_function int 
OsResolveAndOpenFile
(
    OS_FILE_SYSTEM *file_system, 
    char const            *path, 
    int                   usage, 
    uint32_t              hints, 
    OS_FILE               *file, 
    char const  **relative_path
)
{   // iterate over the mount points, which are stored in priority order.
    AcquireSRWLockShared(&file_system->MountpointLock);
    int            result = OS_IO_RESULT_OSERROR;
    OS_MOUNTPOINT *mounts = file_system->MountpointTable.MountpointData;
    for (size_t i = 0,  n = file_system->MountpointTable.Count; i < n; ++i)
    {
        OS_MOUNTPOINT *m  = &mounts[i];
        if (OsMountPointMatchStart(m->Root, path, m->RootLength))
        {   // attempt to open the file on the mount point.
            if  ((result = m->OpenFile(m, path + m->RootLength + 1, usage, hints, file)) == OS_IO_RESULT_SUCCESS)
            {   // the file was successfully opened, so we're done.
                if (relative_path != NULL)
                {
                    *relative_path =  path + m->RootLength + 1;
                }
                ReleaseSRWLockShared(&file_system->MountpointLock);
                return result;
            }
            if (result == OS_IO_RESULT_NOT_SUPPORTED)
            {   // continue checking downstream mount points. one might support the operation.
                continue;
            }
            else if (file->OSError == ERROR_NOT_FOUND || file->OSError == ERROR_FILE_NOT_FOUND)
            {   // continue checking downstream mount points. one might be able to open the file.
                continue;
            }
            else
            {   // an actual error was returned; push it upstream.
                ReleaseSRWLockShared(&file_system->MountpointLock);
                return result;
            }
        }
    }
    ReleaseSRWLockShared(&file_system->MountpointLock);
    file->OSError = ERROR_FILE_NOT_FOUND;
    return OS_IO_RESULT_OSERROR;
}

/// @summary Synchronously writes a file. If the file exists, its contents are overwritten; otherwise a new file is created. This operation is generally not supported by archive-based mount points. This function tests each mount point matching the path, in priority order, until one is successful at saving the file or until all matching mount points have been tested.
/// @param file_system The file system interface.
/// @param path The virtual path of the file to save.
/// @param buffer The buffer containing the data to write to the file.
/// @param amount The number of bytes to copy from the buffer into the file.
/// @param relative_path On return, this pointer is updated to point into path, at the start of the portion of the path representing the path to the file, relative to the mount point root path.
/// @return One of OS_IO_RESULT.
internal_function int 
OsResolveAndSaveFile
(
    OS_FILE_SYSTEM *file_system, 
    char const            *path, 
    void const          *buffer, 
    int64_t              amount, 
    char const  **relative_path
)
{   // iterate over the mount points, which are stored in priority order.
    AcquireSRWLockShared(&file_system->MountpointLock);
    int            result = ERROR_NOT_SUPPORTED;
    OS_MOUNTPOINT *mounts = file_system->MountpointTable.MountpointData;
    for (size_t i = 0,  n = file_system->MountpointTable.Count; i < n; ++i)
    {
        OS_MOUNTPOINT  *m = &mounts[i];
        if (OsMountPointMatchStart(m->Root, path, m->RootLength))
        {   // attempt to save the file on the mount point.
            if ((result = m->SaveFile(m, path + m->RootLength + 1, buffer, amount)) == OS_IO_RESULT_SUCCESS)
            {   // the file was successfully opened, so we're done.
                if (relative_path != NULL)
                {
                    *relative_path =  path + m->RootLength + 1;
                }
                ReleaseSRWLockShared(&file_system->MountpointLock);
                return result;
            }
            if (result == OS_IO_RESULT_NOT_SUPPORTED)
            {   // continue checking downstream mount points. one might support the operation.
                continue;
            }
            else
            {   // an actual error was returned; push it upstream.
                ReleaseSRWLockShared(&file_system->MountpointLock);
                return result;
            }
        }
    }
    ReleaseSRWLockShared(&file_system->MountpointLock);
    return OS_IO_RESULT_NOT_SUPPORTED;
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
/// @param runtime The OS_VULKAN_RUNTIME instance, with the LoaderHandle field set.
/// @return VK_SUCCESS if all exported functions were resolved successfully.
internal_function VkResult
OsResolveVulkanExportFunctions
(
    OS_VULKAN_RUNTIME *runtime
)
{
    OS_LAYER_RESOLVE_VULKAN_ICD_FUNCTION(runtime, vkGetInstanceProcAddr);
    return VK_SUCCESS;
}

/// @summary Resolve global functions exported by a Vulkan ICD.
/// @param runtime The OS_VULKAN_RUNTIME instance, with the LoaderHandle field set.
/// @return VK_SUCCESS if all global functions were resolved successfully.
internal_function VkResult
OsResolveVulkanGlobalFunctions
(
    OS_VULKAN_RUNTIME *runtime
)
{
    OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(runtime, vkCreateInstance);
    OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(runtime, vkEnumerateInstanceLayerProperties);
    OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(runtime, vkEnumerateInstanceExtensionProperties);
    return VK_SUCCESS;
}

/// @summary Resolve instance-level functions exported by a Vulkan runtime.
/// @param runtime The OS_VULKAN_RUNTIME object, with the vkGetInstanceProcAddr entry point set.
/// @param instance The OS_VULKAN_INSTANCE object, with the InstanceHandle field set.
/// @param create_info The VkInstanceCreateInfo being used to initialize the instance. Used here to resolve instance-level function pointers for extensions.
/// @return VK_SUCCESS if all instance-level functions were resolved successfully.
internal_function VkResult
OsResolveVulkanInstanceFunctions
(
    OS_VULKAN_RUNTIME               *runtime,
    OS_VULKAN_INSTANCE             *instance,
    VkInstanceCreateInfo  const *create_info 
)
{
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkCreateDevice);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkDestroyInstance);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkEnumeratePhysicalDevices);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkEnumerateDeviceExtensionProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(instance, runtime, vkEnumerateDeviceLayerProperties);
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
/// @param vkinstance The OS_VULKAN_INSTANCE object, with the vkGetDeviceProcAddr entry point set.
/// @param vkdevice The OS_VULKAN_DEVICE object, with the LogicalDevice field set.
/// @param create_info The VkDeviceCreateInfo being used to initialize the logical device. Used here to resolve device-level function pointers for extensions.
/// @return VkResult if all device-level functions were resolved successfully.
internal_function VkResult
OsResolveVulkanDeviceFunctions
(
    OS_VULKAN_INSTANCE          *instance,
    OS_VULKAN_DEVICE              *device, 
    VkDeviceCreateInfo const *create_info
)
{
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
internal_function size_t
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
internal_function size_t
OsAllocationSizeForArray
(
    size_t n
)
{
    return (sizeof(T) * n) + (std::alignment_of<T>::value - 1);
}

/// @summary Reserve process address space for a memory arena. By default, no address space is committed.
/// @param arena The memory arena to initialize.
/// @param arena_size The number of bytes of process address space to reserve.
/// @param commit_all Specify true to commit the entire reserved range immediately.
/// @param guard_page Specify true to allocate and commit a guard page to detect memory overwrites.
/// @return Zero if the arena is initialized, or -1 if an error occurred.
public_function int
OsCreateMemoryArena
(
    OS_MEMORY_ARENA     *arena, 
    size_t          arena_size,
    bool            commit_all=false, 
    bool            guard_page=false
)
{   // retrieve the system page size and allocation granularity.
    SYSTEM_INFO sys_info = {};
    size_t   commit_size = 0;
    size_t         extra = 0;
    DWORD          flags = MEM_RESERVE;
    void           *base = NULL;

    if (commit_all)
    {   // commit the entire range of address space.
        commit_size = arena_size;
        flags      |= MEM_COMMIT;
    }

    // query the system for the page size and allocation granularity.
    GetNativeSystemInfo(&sys_info);

    // virtual memory allocations are rounded up to the next even multiple of the system
    // page size, and have a starting address that is an even multiple of the system 
    // allocation granularity (SYSTEM_INFO::dwAllocationGranularity).
    arena_size = OsAlignUp(arena_size, size_t(sys_info.dwPageSize));
    if (guard_page)
    {   // add an extra page for use as a guard page.
        extra = sys_info.dwPageSize;
    }

    // reserve (and optionally commit) contiguous virtual address space.
    if ((base = VirtualAlloc(NULL, arena_size + extra, flags, PAGE_READWRITE)) == NULL)
    {   // unable to reserve the requested amount of address space; fail.
        OsLayerError("ERROR: VirtualAlloc for %Iu bytes failed with result 0x%08X.\n", arena_size+extra, GetLastError());
        arena->NextOffset        = 0;
        arena->BytesCommitted    = 0;
        arena->BytesReserved     = 0;
        arena->BaseAddress       = NULL;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        arena->PageSize          = sys_info.dwPageSize;
        arena->Granularity       = sys_info.dwAllocationGranularity;
        return -1;
    }

    if (guard_page)
    {   // commit the guard page, if necessary, and change the protection flags.
        if (VirtualAlloc(((uint8_t*)base + arena_size), sys_info.dwPageSize, MEM_COMMIT, PAGE_NOACCESS) == NULL)
        {   // unable to commit the guard page. consider this a fatal error.
            OsLayerError("ERROR: VirtualAlloc to commit guard page failed with result 0x%08X.\n", GetLastError());
            VirtualFree(base, 0, MEM_RELEASE);
            arena->NextOffset        = 0;
            arena->BytesCommitted    = 0;
            arena->BytesReserved     = 0;
            arena->BaseAddress       = NULL;
            arena->ReserveAlignBytes = 0;
            arena->ReserveTotalBytes = 0;
            arena->PageSize          = sys_info.dwPageSize;
            arena->Granularity       = sys_info.dwAllocationGranularity;
            return -1;
        }
    }

    arena->NextOffset        = 0;
    arena->BytesCommitted    = commit_size;
    arena->BytesReserved     = arena_size;
    arena->BaseAddress       = (uint8_t*) base;
    arena->ReserveAlignBytes = 0;
    arena->ReserveTotalBytes = 0;
    arena->PageSize          = sys_info.dwPageSize;
    arena->Granularity       = sys_info.dwAllocationGranularity;
    return 0;
}

/// @summary Release process address space reserved for a memory arena. All allocations are invalidated.
/// @param arena The memory arena to delete.
public_function void
OsDeleteMemoryArena
(
    OS_MEMORY_ARENA *arena
)
{
    if (arena->BaseAddress != NULL)
    {   // free the entire range of reserved virtual address space.
        VirtualFree(arena->BaseAddress, 0, MEM_RELEASE);
    }
    arena->NextOffset        = 0;
    arena->BytesCommitted    = 0;
    arena->BytesReserved     = 0;
    arena->BaseAddress       = 0;
    arena->ReserveAlignBytes = 0;
    arena->ReserveTotalBytes = 0;
}

/// @summary Query a memory arena for the total number of bytes of process address space reserved for the arena.
/// @param arena The memory arena to query.
/// @return The number of bytes of process address space reserved for use by the arena.
public_function size_t
OsMemoryArenaBytesReserved
(
    OS_MEMORY_ARENA *arena
)
{
    return arena->BytesReserved;
}

/// @summary Query a memory arena for the total number of bytes currently reserved, but not committed.
/// @param arena The memory arena to query.
/// @return The number of bytes of process address space reserved for use by the arena, but not currently committed for use.
public_function size_t
OsMemoryArenaBytesUncommitted
(
    OS_MEMORY_ARENA *arena
)
{
    return (arena->BytesReserved - arena->BytesCommitted);
}

/// @summary Query a memory arena for the total number of bytes currently committed.
/// @param arena The memory arena to query.
/// @return The number of bytes of process address space reserved for use by the arena and currently committed.
public_function size_t
OsMemoryArenaBytesCommitted
(
    OS_MEMORY_ARENA *arena
)
{
    return arena->BytesCommitted;
}

/// @summary Query a memory arena for the number of bytes in the active application reservation.
/// @param arena The memory arena to query.
/// @return The number of bytes in the active application reservation.
public_function size_t
OsMemoryArenaBytesInActiveReservation
(
    OS_MEMORY_ARENA *arena
)
{
    return (arena->ReserveTotalBytes - arena->ReserveAlignBytes);
}

/// @summary Retrieves the virtual memory manager page size for a given arena.
/// @param arena The memory arena to query.
/// @return The operating system page size, in bytes.
public_function size_t
OsMemoryArenaPageSize
(
    OS_MEMORY_ARENA *arena
)
{
    return size_t(arena->PageSize);
}

/// @summary Retrieves the virtual memory manager allocation granularity for a given arena.
/// @param arena The memory arena to query.
/// @return The operating system allocation granularity, in bytes.
public_function size_t
OsMemoryArenaSystemGranularity
(
    OS_MEMORY_ARENA *arena
)
{
    return size_t(arena->Granularity);
}

/// @summary Determine whether a memory allocation request can be satisfied.
/// @param arena The memory arena to query.
/// @param alloc_size The size of the allocation request, in bytes.
/// @param alloc_alignment The desired alignment of the returned address. This must be a power of two greater than zero.
/// @return true if the specified allocation will succeed.
public_function bool
OsMemoryArenaCanSatisfyAllocation
(
    OS_MEMORY_ARENA *arena, 
    size_t      alloc_size,
    size_t alloc_alignment
)
{
    size_t    base_address = size_t(arena->BaseAddress) + arena->NextOffset;
    size_t aligned_address = OsAlignUp(base_address, alloc_alignment);
    size_t     alloc_bytes = alloc_size + (aligned_address - base_address);
    if ((arena->NextOffset + alloc_bytes) > arena->BytesReserved)
    {   // there's not enough space to satisfy the allocation request.
        return false;
    }
    return true;
}

/// @summary Determine whether a memory allocation request can be satisfied.
/// @typeparam T The type being allocated. This type is used to determine the required alignment.
/// @param arena The memory arena to query.
/// @return true if the specified allocation will succeed.
template <typename T>
public_function bool
OsMemoryArenaCanAllocate
(
    OS_MEMORY_ARENA *arena
)
{
    return OsMemoryArenaCanSatisfyAllocation(arena, sizeof(T), std::alignment_of<T>::value);
}

/// @summary Determine whether a memory allocation request for an array can be satisfied.
/// @typeparam T The type of array element. This type is used to determine the required alignment.
/// @param arena The memory arena to query.
/// @param count The number of items in the array.
/// @return true if the specified allocation will succeed.
template <typename T>
public_function inline bool
OsMemoryArenaCanAllocateArray
(
    OS_MEMORY_ARENA *arena,
    size_t           count
)
{
    return OsMemoryArenaCanSatisfyAllocation(arena, sizeof(T) * count, std::alignment_of<T>::value);
}

/// @summary Allocate memory from an arena. Additional address space is committed up to the initial reservation size.
/// @param arena The memory arena to allocate from.
/// @param alloc_size The minimum number of bytes to allocate.
/// @param alloc_alignment A power-of-two, greater than or equal to 1, specifying the alignment of the returned address.
/// @return A pointer to the start of the allocated block, or NULL if the request could not be satisfied.
public_function void*
OsMemoryArenaAllocate
(
    OS_MEMORY_ARENA *arena, 
    size_t      alloc_size, 
    size_t alloc_alignment
)
{
    size_t    base_address = size_t(arena->BaseAddress) + arena->NextOffset;
    size_t aligned_address = OsAlignUp(base_address, alloc_alignment);
    size_t     bytes_total = alloc_size + (aligned_address - base_address);
    if ((arena->NextOffset + bytes_total) > arena->BytesReserved)
    {   // there's not enough reserved address space to satisfy the request.
        OsLayerError("ERROR: %S: Insufficient reserved address space to satisfy request.\n", __FUNCTION__);
        return NULL;
    }
    if ((arena->NextOffset + bytes_total) > arena->BytesCommitted)
    {   // additional address space needs to be committed.
        if (VirtualAlloc(arena->BaseAddress + arena->NextOffset, bytes_total, MEM_COMMIT, PAGE_READWRITE) == NULL)
        {   // there's not enough committed address space to satisfy the request.
            OsLayerError("ERROR: %S: VirtualAlloc failed to commit address space with result 0x%08X.\n", __FUNCTION__, GetLastError());
            return NULL;
        }
        arena->BytesCommitted = OsAlignUp(arena->NextOffset + bytes_total, arena->PageSize);
    }
    arena->NextOffset += bytes_total;
    return (void*) aligned_address;
}

/// @summary Allocate memory for a structure.
/// @typeparam T The type being allocated. This type is used to determine the required alignment.
/// @param arena The memory arena to allocate from.
/// @return A pointer to the new structure, or nullptr if the arena could not satisfy the allocation request.
template <typename T>
public_function T*
OsMemoryArenaAllocate
(
    OS_MEMORY_ARENA *arena
)
{
    return (T*) OsMemoryArenaAllocate(arena, sizeof(T), std::alignment_of<T>::value);
}

/// @summary Allocate memory for an array of structures.
/// @typeparam T The type of array element. This type is used to determine the required alignment.
/// @param arena The memory arena to allocate from.
/// @param count The number of items to allocate.
/// @return A pointer to the start of the array, or nullptr if the arena could not satisfy the allocation request.
template <typename T>
public_function T*
OsMemoryArenaAllocateArray
(
    OS_MEMORY_ARENA *arena, 
    size_t           count
)
{
    return (T*) OsMemoryArenaAllocate(arena, sizeof(T) * count, std::alignment_of<T>::value);
}

/// @summary Reserve memory within an arena. Additional address space is committed up to the initial OS reservation size. Use MemoryArenaCommit to commit the number of bytes actually used.
/// @param arena The memory arena to allocate from.
/// @param reserve_size The number of bytes to reserve for the active allocation.
/// @param alloc_alignment A power-of-two, greater than or equal to 1, specifying the alignment of the returned address.
/// @return A pointer to the start of the allocated block, or NULL if the request could not be satisfied.
public_function void*
OsMemoryArenaReserve
(
    OS_MEMORY_ARENA *arena, 
    size_t    reserve_size, 
    size_t alloc_alignment
)
{
    if (arena->ReserveAlignBytes != 0 || arena->ReserveTotalBytes != 0)
    {   // there's an existing reservation, which must be committed or canceled first.
        OsLayerError("ERROR: %S: Cannot reserve with existing active reservation.\n", __FUNCTION__);
        return NULL;
    }
    size_t base_address    = size_t(arena->BaseAddress) + arena->NextOffset;
    size_t aligned_address = OsAlignUp(base_address, alloc_alignment);
    size_t bytes_total     = reserve_size + (aligned_address - base_address);
    if ((arena->NextOffset + bytes_total) > arena->BytesReserved)
    {   // there's not enough reserved address space to satisfy the request.
        OsLayerError("ERROR: %S: Insufficient reserved address space to satisfy request.\n", __FUNCTION__);
        return NULL;
    }
    if ((arena->NextOffset + bytes_total) > arena->BytesCommitted)
    {   // additional address space needs to be committed.
        if (VirtualAlloc(arena->BaseAddress + arena->NextOffset, bytes_total, MEM_COMMIT, PAGE_READWRITE) == NULL)
        {   // there's not enough committed address space to satisfy the request.
            OsLayerError("ERROR: %S: VirtualAlloc failed to commit address space with result 0x%08X.\n", __FUNCTION__, GetLastError());
            return NULL;
        }
        arena->BytesCommitted = OsAlignUp(arena->NextOffset + bytes_total, arena->PageSize);
    }
    arena->ReserveAlignBytes = aligned_address - base_address;
    arena->ReserveTotalBytes = bytes_total;
    return (void*) aligned_address;
}

/// @summary Reserve storage for an array of items within an arena. The reservation may later be committed or canceled.
/// @typeparam T The type of array element. This type is used to determine the required alignment.
/// @param arena The memory arena to allocate from.
/// @param count The number of elements in the array.
/// @return A pointer to the start of the reservation, or nullptr if the arena could not satisfy the reservation request.
template <typename T>
public_function T*
OsMemoryArenaReserveArray
(
    OS_MEMORY_ARENA *arena, 
    size_t           count
)
{
    return (T*) OsMemoryArenaReserve(arena, sizeof(T) * count, std::alignment_of<T>::value);
}

/// @summary Commits the active reservation within the memory arena.
/// @param arena The memory arena maintaining the reservation.
/// @param commit_size The number of bytes to commit, which must be less than or equal to the reservation size.
/// @return Zero if the commit is successful, or -1 if the commit size is invalid.
public_function int
OsMemoryArenaCommit
(
    OS_MEMORY_ARENA *arena, 
    size_t     commit_size
)
{
    if (commit_size == 0)
    {   // cancel the reservation; don't commit any space.
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return 0;
    }
    if (commit_size <= (arena->ReserveTotalBytes - arena->ReserveAlignBytes))
    {   // the commit size is valid, so commit the space.
        arena->NextOffset       += arena->ReserveAlignBytes + commit_size;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return 0;
    }
    else
    {   // the commit size is not valid, so cancel the outstanding reservation.
        OsLayerError("ERROR: %S: Invalid commit size %Iu (expected <= %Iu); cancelling reservation.\n", __FUNCTION__, commit_size, arena->ReserveTotalBytes-arena->ReserveAlignBytes);
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return -1;
    }
}

/// @summary Commit the active memory reservation, marking some or all of the reserved space as allocated.
/// @typeparam T The type of array element. This type is used to determine the required alignment.
/// @param arena The memory arena maintaining the reservation.
/// @param count The number of array elements to commit. Specify 0 to cancel the outstanding reservation (the memory is marked unallocated.)
/// @return Zero if the commit is successful, or -1 if the commit size is invalid.
template <typename T>
public_function int
OsMemoryArenaCommitArray
(
    OS_MEMORY_ARENA *arena, 
    size_t           count
)
{
    return OsMemoryArenaCommit(arena, sizeof(T) * count, std::alignment_of<T>::value);
}

/// @summary Cancel the active memory arena reservation, indicating that the returned memory block will not be used.
/// @param arena The memory arena maintaining the reservation to be cancelled.
public_function void
OsMemoryArenaCancel
(
    OS_MEMORY_ARENA *arena
)
{
    arena->ReserveAlignBytes = 0;
    arena->ReserveTotalBytes = 0;
}

/// @summary Retrieve an exact marker that can be used to reset or decommit the arena, preserving all current allocations.
/// @param arena The memory arena to query.
/// @return The marker representing the byte offset of the next allocation.
public_function os_arena_marker_t
OsMemoryArenaMark
(
    OS_MEMORY_ARENA *arena
)
{
    return os_arena_marker_t(arena->NextOffset);
}

/// @summary Resets the state of the arena back to a marker, without decommitting any memory.
/// @param arena The memory arena to reset.
/// @param arena_marker The marker value returned by OsMemoryArenaMark().
public_function void
OsMemoryArenaResetToMarker
(
    OS_MEMORY_ARENA         *arena,
    os_arena_marker_t arena_marker
)
{
    if (size_t(arena_marker) <= arena->NextOffset)
    {
        arena->NextOffset = (size_t) arena_marker;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
    }
}

/// @summary Resets the state of the arena to empty, without decomitting any memory.
/// @param arena The memory arena to reset.
public_function void
OsMemoryArenaReset
(
    OS_MEMORY_ARENA *arena
)
{
    arena->NextOffset = 0;
    arena->ReserveAlignBytes = 0;
    arena->ReserveTotalBytes = 0;
}

/// @summary Decommit memory within an arena back to a previously retrieved marker.
/// @param arena The memory arena to decommit.
/// @param arena_marker The marker value returned by OsMemoryArenaMark().
public_function void
OsMemoryArenaDecommitToMarker
(
    OS_MEMORY_ARENA         *arena, 
    os_arena_marker_t arena_marker
)
{
    if (size_t(arena_marker) < arena->NextOffset)
    {   // VirtualFree will decommit any page with at least one byte in the 
        // address range [arena->BaseAddress+arena_marker, arena->BaseAddress+arena->BytesCommitted].
        // since the marker might appear within a page, round the address up to the next page boundary.
        // this avoids decommitting a page that is still partially allocated.
        size_t last_page  = size_t(arena->BaseAddress) + arena->BytesCommitted;
        size_t mark_page  = size_t(arena->BaseAddress) + size_t(arena_marker);
        size_t next_page  = OsAlignUp(mark_page, arena->PageSize);
        size_t free_size  = last_page - next_page;
        arena->NextOffset = size_t(arena_marker);
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        if (free_size > 0)
        {   // the call will decommit at least one page.
            VirtualFree((void*) next_page, free_size, MEM_DECOMMIT);
            arena->BytesCommitted -= free_size;
        }
    }
}

/// @summary Decommit all of the pages within a memory arena, without releasing the address space reservation.
/// @param arena The memory arena to decommit.
public_function void
OsMemoryArenaDecommit
(
    OS_MEMORY_ARENA *arena
)
{
    if (arena->BytesCommitted > 0)
    {   // decommit the entire committed region of address space.
        VirtualFree(arena->BaseAddress, 0, MEM_DECOMMIT);
        arena->NextOffset = 0;
        arena->BytesCommitted = 0;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
    }
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
/// @param arena The memory arena used to allocate temporary memory. The temporary memory is freed before the function returns.
/// @return true if the host CPU information was successfully retrieved.
public_function bool
OsQueryHostCpuLayout
(
    OS_CPU_INFO  *cpu_info, 
    OS_MEMORY_ARENA *arena
)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *lpibuf = NULL;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info   = NULL;
    os_arena_marker_t mm = OsMemoryArenaMark(arena);
    size_t     alignment = std::alignment_of<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>::value;
    size_t     smt_count = 0;
    uint8_t     *bufferp = NULL;
    uint8_t     *buffere = NULL;
    DWORD    buffer_size = 0;
    int          regs[4] ={0, 0, 0, 0};

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
    if (!OsMemoryArenaCanSatisfyAllocation(arena, buffer_size, alignment))
    {
        OsLayerError("ERROR: %S: Insufficient memory to query host CPU layout.\n", __FUNCTION__);
        cpu_info->NumaNodes       = 1;
        cpu_info->PhysicalCPUs    = 1;
        cpu_info->PhysicalCores   = 1;
        cpu_info->HardwareThreads = 1;
        cpu_info->ThreadsPerCore  = 1;
        return false;
    }
    lpibuf = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) OsMemoryArenaAllocate(arena, buffer_size, alignment);
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
    // free the temporary buffer:
    OsMemoryArenaResetToMarker(arena, mm);

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
    OsThreadEvent(init.ThreadPool, "Worker thread %u starting", tid);

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
            // reset the wake signal to 0/NULL for the next iteration.
            signal_arg = 0;
        }
    }
    __finally
    {   // the worker is terminating - clean up thread-local resources.
        // the AppThreadMain will not be called again by this thread.
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
OsCalculateMemoryForThreadPool
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
    size_t        bytes_required = OsCalculateMemoryForThreadPool(init->ThreadCount);
    size_t        align_required = std::alignment_of<HANDLE>::value;
    CV_PROVIDER     *cv_provider = NULL;
    CV_MARKERSERIES   *cv_series = NULL;
    HRESULT            cv_result = S_OK;
    char             cv_name[64] = {};

    if (!OsMemoryArenaCanSatisfyAllocation(arena, bytes_required, align_required))
    {
        OsLayerError("ERROR: %S(%u): Insufficient memory to create thread pool.\n", __FUNCTION__, tid);
        OsZeroMemory(pool, sizeof(OS_THREAD_POOL));
        return -1;
    }
    if ((evt_launch = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // without the launch event, there's no way to synchronize worker launch.
        OsLayerError("ERROR: %S(%u): Unable to create pool launch event (0x%08X).\n", __FUNCTION__, tid, GetLastError());
        OsZeroMemory(pool, sizeof(OS_THREAD_POOL));
        return -1;
    }
    if ((evt_terminate = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // without the termination event, there's no way to synchronize worker shutdown.
        OsLayerError("ERROR: %S(%u): Unable to create pool termination event (0x%08X).\n", __FUNCTION__, tid, GetLastError());
        OsZeroMemory(pool, sizeof(OS_THREAD_POOL));
        CloseHandle(evt_launch);
        return -1;
    }
    if ((iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, (DWORD) init->ThreadCount+1)) == NULL)
    {   // without the completion port, there's no way to synchronize worker execution.
        OsLayerError("ERROR: %S(%u): Unable to create pool I/O completion port (0x%08X).\n", __FUNCTION__, tid, GetLastError());
        OsZeroMemory(pool, sizeof(OS_THREAD_POOL));
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
        OsZeroMemory(pool, sizeof(OS_THREAD_POOL));
        CloseHandle(evt_terminate);
        CloseHandle(evt_launch);
        CloseHandle(iocp);
        return -1;
    }
    if (!SUCCEEDED((cv_result = CvCreateMarkerSeriesA(cv_provider, name, &cv_series))))
    {
        OsLayerError("ERROR: %S(%u): Unable to create task profiler marker series (HRESULT 0x%08X).\n", __FUNCTION__, tid, cv_result);
        OsZeroMemory(pool, sizeof(OS_THREAD_POOL));
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
    pool->TaskProfiler.Provider      = cv_provider;
    pool->TaskProfiler.MarkerSeries  = cv_series;
    OsZeroMemory(pool->OSThreadIds   , init->ThreadCount * sizeof(unsigned int));
    OsZeroMemory(pool->OSThreadHandle, init->ThreadCount * sizeof(HANDLE));
    OsZeroMemory(pool->WorkerReady   , init->ThreadCount * sizeof(HANDLE));
    OsZeroMemory(pool->WorkerError   , init->ThreadCount * sizeof(HANDLE));

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
    OsZeroMemory(pool, sizeof(OS_THREAD_POOL));
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
        OsZeroMemory(pool->OSThreadIds   , pool->ActiveThreads * sizeof(unsigned int));
        OsZeroMemory(pool->OSThreadHandle, pool->ActiveThreads * sizeof(HANDLE));
        OsZeroMemory(pool->WorkerReady   , pool->ActiveThreads * sizeof(HANDLE));
        OsZeroMemory(pool->WorkerError   , pool->ActiveThreads * sizeof(HANDLE));
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

/// @summary Parse a TAR file and construct the list of file and directory entries. Tarballs are intended to be unloadable, so a memory arena is not used.
/// @param tar The TAR file mountpoint state to populate with entity data.
/// @param tarfd The file descriptor to access for reading the TAR file data.
/// @param result If the function returns -1, the system error code is returned in this location.
/// @return Zero if the tarball loaded successfully, or -1 if an error occurred.
public_function int 
OsLoadTarball
(
    OS_TARBALL   *tar, 
    HANDLE      tarfd, 
    DWORD      result
)
{
    LARGE_INTEGER offset = {};
    LARGE_INTEGER newptr = {};
    LARGE_INTEGER startp = {};
    size_t   entry_count = 0;

    // initialize the fields of the OS_TARBALL instance.
    ZeroMemory(tar, sizeof(OS_TARBALL));

    // save the offset of the start of the archive data.
    if (!SetFilePointerEx(tarfd, offset, &startp, FILE_CURRENT))
    {
        OsLayerError("ERROR: %S(%u): Unable to retrieve the current file position (%08X).\n", __FUNCTION__, GetCurrentThreadId(), GetLastError());
        result = GetLastError();
        goto cleanup_and_fail;
    }

    // the first pass counts all of the regular file entries in the archive.
    offset.QuadPart = startp.QuadPart;
    newptr.QuadPart = startp.QuadPart;
    for ( ; ; )
    {   // read the next archive entry header and check for end-of-archive.
        DWORD nread = 0;
        OS_TAR_HEADER_ENCODED header  = {};
        if (!ReadFile(tarfd, &header, sizeof(header), &nread, NULL) || nread < sizeof(header))
        {   // unable to read a complete header block.
            OsLayerError("ERROR: %S(%u): Unable to read complete TAR header block at %I64d (%08X).\n", __FUNCTION__, GetCurrentThreadId(), offset.QuadPart, GetLastError());
            result = GetLastError();
            goto cleanup_and_fail;
        }
        if (header.FileName[0] == 0)
        {   // an entry with no filename indicates the end of the archive.
            result = ERROR_SUCCESS;
            break;
        }
        if (header.FileType == OS_TAR_ENTRY_TYPE_FILE)
        {
            entry_count++;
        }

        // seek to the start of the next archive entry.
        int64_t  header_end  = offset.QuadPart + sizeof(OS_TAR_HEADER_ENCODED);
        int64_t   file_size  = OsTarOctalToDecimal<int64_t>(header.FileSize, 12);
        offset.QuadPart      = OsTarNextHeaderOffset(header_end, file_size);
        if (!SetFilePointerEx(tarfd, offset, &newptr, FILE_BEGIN))
        {
            OsLayerError("ERROR: %S(%u): Unable to seek to start of next TAR header block at %I64d (%08X).\n", __FUNCTION__, GetCurrentThreadId(), offset.QuadPart, GetLastError());
            result = GetLastError();
            goto cleanup_and_fail;
        }
    }

    // allocate storage and initialize the OS_TARBALL instance.
    tar->EntryCount     = 0;
    tar->EntryHash      =(uint32_t    *) malloc(entry_count * sizeof(uint32_t));
    tar->EntryInfo      =(OS_TAR_ENTRY*) malloc(entry_count * sizeof(OS_TAR_ENTRY));
    if (tar->EntryHash == NULL || tar->EntryInfo == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate memory for %Iu TAR entries.\n", __FUNCTION__, GetCurrentThreadId(), entry_count);
        result = ERROR_OUTOFMEMORY;
        goto cleanup_and_fail;
    }

    // seek back to the start of the archive data, and re-parse all entries.
    if (!SetFilePointerEx(tarfd, startp, NULL, FILE_BEGIN))
    {
        OsLayerError("ERROR: %S(%u): Unable to rewind to the start of TAR data (%08X).\n", __FUNCTION__, GetCurrentThreadId(), GetLastError());
        result = GetLastError();
        goto cleanup_and_fail;
    }
    offset.QuadPart = startp.QuadPart;
    newptr.QuadPart = startp.QuadPart;
    for ( ; ; )
    {   // read the next archive entry header and check for end-of-archive.
        DWORD nread = 0;
        OS_TAR_HEADER_ENCODED header  = {};
        if (!ReadFile(tarfd, &header, sizeof(header), &nread, NULL) || nread < sizeof(header))
        {   // unable to read a complete header block.
            OsLayerError("ERROR: %S(%u): Unable to read complete TAR header block at %I64d (%08X).\n", __FUNCTION__, GetCurrentThreadId(), offset.QuadPart, GetLastError());
            result = GetLastError();
            goto cleanup_and_fail;
        }
        if (header.FileName[0] == 0)
        {   // an entry with no filename indicates the end of the archive.
            result = ERROR_SUCCESS;
            break;
        }
        if (header.FileType == OS_TAR_ENTRY_TYPE_FILE)
        {   // extract information for file entries.
            size_t entry_index = tar->EntryCount;
            OsTarDecodeEntry(&tar->EntryInfo[entry_index], &header, offset.QuadPart);
            tar->EntryHash[entry_index] = OsHashPath(tar->EntryInfo[entry_index].FullPath);
            tar->EntryCount++;
        }

        // seek to the start of the next archive entry.
        int64_t  header_end  = offset.QuadPart + sizeof(OS_TAR_HEADER_ENCODED);
        int64_t   file_size  = OsTarOctalToDecimal<int64_t>(header.FileSize, 12);
        offset.QuadPart      = OsTarNextHeaderOffset(header_end, file_size);
        if (!SetFilePointerEx(tarfd, offset, &newptr, FILE_BEGIN))
        {
            OsLayerError("ERROR: %S(%u): Unable to seek to start of next TAR header block at %I64d (%08X).\n", __FUNCTION__, GetCurrentThreadId(), offset.QuadPart, GetLastError());
            result = GetLastError();
            goto cleanup_and_fail;
        }
    }
    return 0;

cleanup_and_fail:
    SetFilePointerEx(tarfd, startp, NULL, FILE_BEGIN);
    if (tar->EntryInfo != NULL) free(tar->EntryInfo);
    if (tar->EntryHash != NULL) free(tar->EntryHash);
    ZeroMemory(tar, sizeof(OS_TARBALL));
    return -1;
}

/// @summary Free resources associated with a loaded tarball.
/// @param tar The OS_TARBALL instance to unload.
public_function void
OsUnloadTarball
(
    OS_TARBALL *tar
)
{
    if (tar->EntryInfo != NULL)
    {
        free(tar->EntryInfo);
        tar->EntryInfo = NULL;
    }
    if (tar->EntryHash != NULL)
    {
        free(tar->EntryHash);
        tar->EntryHash = NULL;
    }
    tar->EntryCount = 0;
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

/// @summary Attempt to load the LunarG Vulkan loader and resolve all exported and global functions used to create a Vulkan instance or enumerate runtime layers and extensions.
/// @param runtime The Vulkan runtime instance to populate with function pointers.
/// @return VK_SUCCESS if Vulkan is available on the host system.
public_function VkResult
OsLoadVulkanRuntime
(
    OS_VULKAN_RUNTIME *runtime
)
{
    HMODULE  dll_instance = NULL;
    VkResult       result = VK_SUCCESS;

    // initialize all of the fields of the runtime loader instance.
    ZeroMemory(runtime, sizeof(OS_VULKAN_RUNTIME));

    // attempt to load the LunarG Vulkan loader into the process address space.
    if ((dll_instance = LoadLibrary(_T("vulkan-1.dll"))) == NULL)
    {   // if the LunarG loader isn't available, assume no ICD is present.
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // initialize the base loader object fields. this is required prior to resolving additional entry points.
    runtime->LoaderHandle = dll_instance;

    // resolve the core Vulkan loader entry points.
    if ((result = OsResolveVulkanExportFunctions(runtime)) != VK_SUCCESS)
    {   // the loader will be unable to load additional required entry points or create an instance.
        CloseHandle(dll_instance);
        ZeroMemory(runtime, sizeof(OS_VULKAN_RUNTIME));
        return result;
    }
    if ((result = OsResolveVulkanGlobalFunctions(runtime)) != VK_SUCCESS)
    {   // the loader will be unable to create an instance or enumerate devices.
        CloseHandle(dll_instance);
        ZeroMemory(runtime, sizeof(OS_VULKAN_RUNTIME));
        return result;
    }

    return VK_SUCCESS;
}

/// @summary Retrieve the validation layers and extensions supported by the Vulkan runtime,
/// @param props The runtime properties structure to populate.
/// @param runtime A valid OS_VULKAN_RUNTIME structure with global function pointers set.
/// @param arena The memory arena to use to allocate memory for the runtime property data.
/// @return VK_SUCCESS if the operation is successful.
public_function VkResult
OsQueryVulkanRuntimeProperties
(
    OS_VULKAN_RUNTIME_PROPERTIES *props, 
    OS_VULKAN_RUNTIME          *runtime, 
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
/// @param instance The OS_VULKAN_INSTANCE to initialize.
/// @param runtime A valid OS_VULKAN_RUNTIME structure with global function pointers set.
/// @param create_info The VkInstanceCreateInfo to pass to vkCreateInstance.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateInstance.
/// @param result If the function returns OS_VULKAN_LOADER_RESULT_VKERROR, the Vulkan result code is stored at this location.
/// @return One of OS_VULKAN_LOADER_RESULT indicating the result of the operation.
public_function VkResult
OsCreateVulkanInstance
(
    OS_VULKAN_INSTANCE                      *instance,
    OS_VULKAN_RUNTIME                        *runtime, 
    VkInstanceCreateInfo  const          *create_info, 
    VkAllocationCallbacks const *allocation_callbacks
)
{
    VkResult result = VK_SUCCESS;

    // initialize all of the fields of the Vulkan instance.
    ZeroMemory(instance, sizeof(OS_VULKAN_INSTANCE));

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
        ZeroMemory(instance, sizeof(OS_VULKAN_INSTANCE));
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
/// @param instance A valid OS_VULKAN_INSTANCE structure with instance-level function pointers set.
/// @param arena The memory arena to use to allocate memory for the runtime property data.
/// @param exe_instance The HINSTANCE passed to the WinMain of the application.
/// @return VK_SUCCESS if the operation is successful.
public_function VkResult
OsEnumerateVulkanPhysicalDevices
(
    OS_VULKAN_PHYSICAL_DEVICE_LIST *device_list, 
    OS_VULKAN_INSTANCE                *instance,
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
    device_list->DeviceLayerCount                    = OsMemoryArenaAllocateArray<size_t                          >(arena, device_count);
    device_list->DeviceLayerProperties               = OsMemoryArenaAllocateArray<VkLayerProperties*              >(arena, device_count);
    device_list->DeviceLayerExtensionCount           = OsMemoryArenaAllocateArray<size_t*                         >(arena, device_count);
    device_list->DeviceLayerExtensionProperties      = OsMemoryArenaAllocateArray<VkExtensionProperties**         >(arena, device_count);
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
        device_list->DeviceLayerCount               == NULL || 
        device_list->DeviceLayerProperties          == NULL || 
        device_list->DeviceLayerExtensionCount      == NULL || 
        device_list->DeviceLayerExtensionProperties == NULL || 
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
        uint32_t      layer_count = 0;
        uint32_t  layer_ext_count = 0;
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
        if ((result = instance->vkEnumerateDeviceLayerProperties(handle, &layer_count, NULL)) < 0)
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve the number of layers exposed by Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_list->DeviceProperties[i].deviceName, result);
            goto cleanup_and_fail;
        }
        if ((result = instance->vkEnumerateDeviceExtensionProperties(handle, NULL, &extension_count, NULL)) < 0)
        {
            OsLayerError("ERROR: %S(%u): Unable to retrieve the number of extensions exposed by Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_list->DeviceProperties[i].deviceName, result);
            goto cleanup_and_fail;
        }
        device_list->DeviceLayerCount[i] = layer_count;
        device_list->DeviceExtensionCount[i] = extension_count;
        device_list->DeviceLayerProperties[i] = NULL;
        device_list->DeviceExtensionProperties[i] = NULL;
        if (layer_count != 0)
        {   // retrieve the layer and layer extension information.
            device_list->DeviceLayerProperties[i]               = OsMemoryArenaAllocateArray<VkLayerProperties     >(arena, layer_count);
            device_list->DeviceLayerExtensionCount[i]           = OsMemoryArenaAllocateArray<size_t                >(arena, layer_count);
            device_list->DeviceLayerExtensionProperties[i]      = OsMemoryArenaAllocateArray<VkExtensionProperties*>(arena, layer_count);
            if (device_list->DeviceLayerProperties[i]          == NULL || 
                device_list->DeviceLayerExtensionCount[i]      == NULL || 
                device_list->DeviceLayerExtensionProperties[i] == NULL)
            {
                OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device layer properties.\n", __FUNCTION__, GetCurrentThreadId());
                result = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup_and_fail;
            }
            if ((result = instance->vkEnumerateDeviceLayerProperties(handle, &layer_count, device_list->DeviceLayerProperties[i])) < 0)
            {
                OsLayerError("ERROR: %S(%u): Unable to retrieve layer information for Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_list->DeviceProperties[i].deviceName, result);
                goto cleanup_and_fail;
            }
            for (size_t j = 0, m = layer_count; j < m; ++j)
            {   // retrieve the number of layer-level extensions for this layer.
                if ((result = instance->vkEnumerateDeviceExtensionProperties(handle, device_list->DeviceLayerProperties[i][j].layerName, &layer_ext_count, NULL)) < 0)
                {
                    OsLayerError("ERROR: %S(%u): Unable to retrieve the number of extensions exposed by layer %S on Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_list->DeviceLayerProperties[i][j].layerName, device_list->DeviceProperties[i].deviceName, result);
                    goto cleanup_and_fail;
                }
                device_list->DeviceLayerExtensionCount[i][j] = layer_ext_count;
                device_list->DeviceLayerExtensionProperties[i][j] = NULL;
                if (layer_ext_count != 0)
                {   // retrieve the layer-level device extension information.
                    if ((device_list->DeviceLayerExtensionProperties[i][j] = OsMemoryArenaAllocateArray<VkExtensionProperties>(arena, layer_ext_count)) == NULL)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device layer extension list.\n", __FUNCTION__, GetCurrentThreadId());
                        result = VK_ERROR_OUT_OF_HOST_MEMORY;
                        goto cleanup_and_fail;
                    }
                    if ((result = instance->vkEnumerateDeviceExtensionProperties(handle, device_list->DeviceLayerProperties[i][j].layerName, &layer_ext_count, device_list->DeviceLayerExtensionProperties[i][j])) < 0)
                    {
                        OsLayerError("ERROR: %S(%u): Unable to retrieve extension information for layer %S on Vulkan physical device %S (VkResult = %08X).\n", __FUNCTION__, GetCurrentThreadId(), device_list->DeviceLayerProperties[i][j].layerName, device_list->DeviceProperties[i].deviceName, result);
                        goto cleanup_and_fail;
                    }
                }
            }
        }
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
/// @param device The OS_VULKAN_DEVICE to initialize.
/// @param instance A valid OS_VULKAN_INSTANCE structure with instance function pointers set.
/// @param physical_device The handle of the Vulkan-capable physical device to associate with the logical device.
/// @param create_info The VkDeviceCreateInfo to pass to vkCreateDevice.
/// @param allocation_callbacks The VkAllocationCallbacks to pass to vkCreateInstance.
/// @param result If the function returns OS_VULKAN_LOADER_RESULT_VKERROR, the Vulkan result code is stored at this location.
/// @return One of OS_VULKAN_LOADER_RESULT indicating the result of the operation.
public_function VkResult
OsCreateVulkanLogicalDevice
(
    OS_VULKAN_DEVICE                          *device,
    OS_VULKAN_INSTANCE                      *instance,
    VkPhysicalDevice                  physical_device, 
    VkDeviceCreateInfo    const          *create_info, 
    VkAllocationCallbacks const *allocation_callbacks
)
{
    VkResult result = VK_SUCCESS;

    // initialize all of the fields of the Vulkan logical device.
    ZeroMemory(device, sizeof(OS_VULKAN_DEVICE));

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
        ZeroMemory(device, sizeof(OS_VULKAN_DEVICE));
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

/// @summary Retrieve a system known folder path using SHGetKnownFolderPath.
/// @param buf The buffer to which the path will be copied.
/// @param buf_bytes The maximum number of bytes that can be written to buf.
/// @param bytes_needed On return, this location stores the number of bytes required to store the path string, including the zero terminator.
/// @param folder_id The Windows Shell known folder identifier.
/// @return true if the requested path was retrieved.
public_function bool 
OsShellFolderPath
(
    WCHAR                 *buf, 
    size_t           buf_bytes, 
    size_t       &bytes_needed, 
    REFKNOWNFOLDERID folder_id
)
{
    WCHAR *sysbuf = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(folder_id, KF_FLAG_NO_ALIAS, NULL, &sysbuf)))
    {   // calculate bytes needed and copy to user buffer.
        if (buf != NULL)  memset(buf, 0, buf_bytes);
        else buf_bytes =  0;
        bytes_needed   = (wcslen(sysbuf) + 1) * sizeof(WCHAR);
        if (buf_bytes >= (bytes_needed + 1))
        {   // the path will fit, so copy it over.
            memcpy(buf, sysbuf, bytes_needed);
            buf[bytes_needed] = 0;
        }
        CoTaskMemFree(sysbuf);
        return true;
    }
    else
    {   // unable to retrieve the specified path.
        bytes_needed = 0;
        return false;
    }
}

/// @summary Retrieve a special system folder path.
/// @param buf The buffer to which the path will be copied.
/// @param buf_bytes The maximum number of bytes that can be written to buf.
/// @param bytes_needed On return, this location stores the number of bytes required to store the path string, including the zero terminator.
/// @param folder_id One of OS_KNOWN_PATH identifying the folder.
/// @return true if the requested path was retrieved.
public_function bool 
OsKnownPath
(
    WCHAR           *buf, 
    size_t     buf_bytes, 
    size_t &bytes_needed, 
    int        folder_id
)
{
    switch (folder_id)
    {
    case OS_KNOWN_PATH_EXECUTABLE:
        {   // zero out the path buffer, and retrieve the EXE path.
            size_t bufsiz =  32768 *  sizeof(WCHAR);
            WCHAR *sysbuf = (WCHAR *) malloc(bufsiz);
            if (sysbuf == NULL)
            {   // unable to allocate enough space for the temporary buffer.
                bytes_needed = 0;
                return false;
            }
            memset(sysbuf, 0, bufsiz);
            size_t buflen = GetModuleFileNameW(GetModuleHandle(NULL), sysbuf, 32768 * sizeof(WCHAR));
            bytes_needed  =(buflen + 1) * sizeof(WCHAR); // convert characters to bytes, add null byte
            // now strip off the name of the executable.
            WCHAR *bufitr = sysbuf + buflen - 1;
            while (bufitr > sysbuf)
            {
                if (*bufitr == L'\\' || *bufitr == L'/')
                {   // replace the trailing slash with a NULL terminator.
                    *bufitr  = 0;
                    break;
                }
                --bytes_needed;
                --bufitr; 
            }
            // finally, copy the path and directory name into the output buffer.
            if  (buf != NULL) memset(buf, 0 ,  buf_bytes);
            else buf_bytes  = 0;
            if  (buf_bytes >= bytes_needed)
            {   // the path will fit, so copy it over.
                memcpy(buf, sysbuf, bytes_needed);
            }
            free(sysbuf);
            return true;
        }
    case OS_KNOWN_PATH_USER_HOME:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_Profile);
    case OS_KNOWN_PATH_USER_DESKTOP:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_Desktop);
    case OS_KNOWN_PATH_USER_DOCUMENTS:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_Documents);
    case OS_KNOWN_PATH_USER_DOWNLOADS:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_Downloads);
    case OS_KNOWN_PATH_USER_MUSIC:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_Music);
    case OS_KNOWN_PATH_USER_PICTURES:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_Pictures);
    case OS_KNOWN_PATH_USER_SAVE_GAMES:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_SavedGames);
    case OS_KNOWN_PATH_USER_VIDEOS:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_Videos);
    case OS_KNOWN_PATH_USER_PREFERENCES:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_LocalAppData);
    case OS_KNOWN_PATH_PUBLIC_DOCUMENTS:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_PublicDocuments);
    case OS_KNOWN_PATH_PUBLIC_DOWNLOADS:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_PublicDownloads);
    case OS_KNOWN_PATH_PUBLIC_MUSIC:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_PublicMusic);
    case OS_KNOWN_PATH_PUBLIC_PICTURES:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_PublicPictures);
    case OS_KNOWN_PATH_PUBLIC_VIDEOS:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_PublicVideos);
    case OS_KNOWN_PATH_SYSTEM_FONTS:
        return OsShellFolderPath(buf, buf_bytes, bytes_needed, FOLDERID_Fonts);

    default:
        break;
    }
    bytes_needed = 0;
    return false;
}

/// @summary Retrieve the physical sector size for a block-access device.
/// @param file The handle to an open file on the device.
/// @return The size of a physical sector on the specified device.
public_function size_t 
OsPhysicalSectorSize
(
    HANDLE file
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
    BOOL result = DeviceIoControl(file, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &desc , sizeof(desc), &bytes, NULL);
    return result ? desc.BytesPerPhysicalSector : DefaultPhysicalSectorSize;
}

/// @summary Initializes a file system driver.
/// @param driver The file system driver to initialize.
/// @param init Configuration data used to initialize the filesystem implementation.
/// @param arena The OS_MEMORY_ARENA used to allocate filesystem resources.
/// @return Zero on success, or -1 if an error occurred.
public_function int 
OsCreateFilesystem
(
    OS_FILE_SYSTEM              *fs,
    OS_FILE_SYSTEM_INIT const *init,
    OS_MEMORY_ARENA          *arena
)
{
    os_arena_marker_t marker = OsMemoryArenaMark(arena);

    // initialize the fields of the filesystem object.
    ZeroMemory(fs, sizeof(OS_FILE_SYSTEM));
    
    // create the table used for storing mount point data.
    InitializeSRWLock(&fs->MountpointLock);
    if (OsCreateMountpointTable(&fs->MountpointTable, init->MaxMountPoints) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate memory for %Iu-entry mount point table.\n", __FUNCTION__, GetCurrentThreadId(), init->MaxMountPoints);
        goto cleanup_and_fail;
    }
    // ...
    return 0;

cleanup_and_fail:
    OsMemoryArenaResetToMarker(arena, marker);
    ZeroMemory(fs, sizeof(OS_FILE_SYSTEM));
    return -1;
}

/// @summary Creates a mount point backed by a well-known directory.
/// @param fs The file system driver.
/// @param folder_id One of OS_KNOWN_PATH specifying the well-known directory.
/// @param mount_path The NULL-terminated UTF-8 string specifying the root of the mount point as exposed to the application code. Multiple mount points may share the same mount_path, but have different source_path, mount_id and priority values.
/// @param priority The priority assigned to this specific mount point, with higher numeric values corresponding to higher priority values.
/// @param mount_id A unique application-defined identifier for the mount point. This identifier can be used to reference the specific mount point.
/// @return Zero if the mount point was successfully created, or -1 if an error occurred.
public_function int 
OsMountKnownPath
(
    OS_FILE_SYSTEM     *fs, 
    int          folder_id, 
    char const *mount_path, 
    uint32_t      priority, 
    uintptr_t     mount_id
)
{   // retrieve the path specified by folder_id for use as the source path.
    size_t const MAX_PATH_CHARS = 32768;
    size_t         source_bytes = 0;
    size_t         buffer_bytes = MAX_PATH_CHARS * sizeof(WCHAR);
    WCHAR  source_wide[MAX_PATH_CHARS]; // 64KB - probably overkill
    if (!OsKnownPath(source_wide, buffer_bytes, source_bytes, folder_id))
    {   // unrecognized folder_id.
        return -1;
    }
    if (OsSetupMountpoint(fs, source_wide, FILE_ATTRIBUTE_DIRECTORY, mount_path, priority, mount_id) < 0)
    {   // unable to finish internal setup of the mount point.
        return -1;
    }
    return 0;
}

/// @summary Creates a mount point backed by the specified archive file or directory.
/// @param fs The file system driver.
/// @param source_path The NULL-terminated UTF-8 string specifying the path of the file or directory that represents the root of the mount point.
/// @param mount_path The NULL-terminated UTF-8 string specifying the root of the mount point as exposed to the application code. Multiple mount points may share the same mount_path, but have different source_path, mount_id and priority values.
/// @param priority The priority assigned to this specific mount point, with higher numeric values corresponding to higher priority values.
/// @param mount_id A unique application-defined identifier for the mount point. This identifier can be used to reference the specific mount point.
/// @return Zero if the mount point was successfully created, or -1 if an error occurred.
public_function int
OsMountPhysicalPath
(
    OS_FILE_SYSTEM      *fs, 
    char const *source_path, 
    char const  *mount_path, 
    uint32_t       priority, 
    uintptr_t      mount_id
)
{   // convert the source path to a wide character string once during mounting.
    size_t source_chars = 0;
    size_t source_bytes = 0;
    DWORD  source_attr  = 0;
    WCHAR *source_wide  = NULL;
    if   ((source_wide  = OsUtf8ToNativeMalloc(source_path, source_chars, source_bytes)) == NULL)
    {   // unable to convert UTF-8 to wide character string.
        goto cleanup_and_fail;
    }

    // determine whether the source path is a directory or a file.
    // TODO(rlk): GetFileAttributes() won't work (GetLastError() => ERROR_BAD_NETPATH)
    // in that case that source_path is the root of a network share. maybe fix this.
    if ((source_attr = GetFileAttributesW(source_wide)) == INVALID_FILE_ATTRIBUTES)
    {   // the source path must exist. mount fails.
        OsLayerError("ERROR: %S(%u): Unable to retrieve attributes for path %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), source_wide, GetLastError());
        goto cleanup_and_fail;
    }
    if (OsSetupMountpoint(fs, source_wide, source_attr, mount_path, priority, mount_id) < 0)
    {   // unable to finish internal setup of the mount point.
        goto cleanup_and_fail;
    }

    // the wide character string is no longer needed.
    free(source_wide);
    return 0;

cleanup_and_fail:
    if (source_wide != NULL) free(source_wide);
    return -1;
}

/// @summary Creates a mount point backed by the specified archive or directory, specified as a virtual path.
/// @param fs The file system driver.
/// @param virtual_path The NULL-terminated UTF-8 string specifying the virtual path of the file or directory that represents the root of the mount point.
/// @param mount_path The NULL-terminated UTF-8 string specifying the root of the mount point as exposed to the application code. Multiple mount points may share the same mount_path, but have different source_path, mount_id and priority values.
/// @param priority The priority assigned to this specific mount point, with higher numeric values corresponding to higher priority values.
/// @param mount_id A unique application-defined identifier for the mount point. This identifier can be used to reference the specific mount point.
/// @return Zero if the mount point was successfully created, or -1 if an error occurred.
public_function int 
OsMountVirtualPath
(
    OS_FILE_SYSTEM       *fs, 
    char const *virtual_path, 
    char const   *mount_path, 
    uint32_t        priority, 
    uintptr_t       mount_id
)
{   // convert the virtual path to a native filesystem path.
    size_t const      MAX_PATH_CHARS = 32768;
    DWORD                source_attr = 0;
    WCHAR source_wide[MAX_PATH_CHARS]; // 64KB - probably overkill
    if (OsResolveFilesystemPath(fs, virtual_path, source_wide, MAX_PATH_CHARS) < 0)
    {   // cannot resolve the virtual path to a filesystem mount point.
        return -1;
    }

    // determine whether the source path is a directory or a file.
    // TODO(rlk): GetFileAttributes() won't work (GetLastError() => ERROR_BAD_NETPATH)
    // in that case that source_path is the root of a network share. maybe fix this.
    if ((source_attr = GetFileAttributesW(source_wide)) == INVALID_FILE_ATTRIBUTES)
    {   // the source path must exist. mount fails.
        OsLayerError("ERROR: %S(%u): Unable to retrieve attributes for path %s (%08X).\n", __FUNCTION__, GetCurrentThreadId(), source_wide, GetLastError());
        return -1;
    }
    if (OsSetupMountpoint(fs, source_wide, source_attr, mount_path, priority, mount_id) < 0)
    {   // unable to finish internal setup of the mount point.
        return -1;
    }
    return 0;
}

/// @summary Removes a mount point associated with a specific application mount point identifier.
/// @param fs The file system driver.
/// @param mount_id The application-defined mount point identifer, as supplied to one of the OsMount* functions.
public_function void 
OsUnmount
(
    OS_FILE_SYSTEM *fs, 
    uintptr_t mount_id
)
{
    AcquireSRWLockExclusive(&fs->MountpointLock);
    OsRemoveMountpointId(&fs->MountpointTable, mount_id);
    ReleaseSRWLockExclusive(&fs->MountpointLock);
}

/// @summary Deletes all mount points attached to a given root path.
/// @param fs The file system driver.
/// @param mount_path A NULL-terminated UTF-8 string specifying the mount point root, as was supplied to one of the OsMount* functions. All mount points that were mounted to this path will be deleted.
public_function void 
OsUnmountAll
(
    OS_FILE_SYSTEM     *fs, 
    char const *mount_path
)
{   // create version of mount_path with trailing slash.
    size_t len        = strlen(mount_path);
    char  *mount_root = (char*)mount_path;
    if (len > 0 && mount_path[len-1]  != '/')
    {   // allocate a temporary buffer on the stack.
        mount_root = (char*)alloca(len + 2);
        memcpy(mount_root , mount_path , len);
        mount_root[len]   = '/';
        mount_root[len+1] = 0;
    }
    // search for matches and remove items in reverse.
    // this minimizes data movement and simplifies logic.
    AcquireSRWLockExclusive(&fs->MountpointLock);
    for (intptr_t i = fs->MountpointTable.Count - 1; i >= 0; --i)
    {
        if (OsMountPointMatchExact(fs->MountpointTable.MountpointData[i].Root, mount_root))
        {   // the mount point paths match, so remove this item.
            OsRemoveMountpointAt(&fs->MountpointTable, i);
        }
    }
    ReleaseSRWLockExclusive(&fs->MountpointLock);
}

/// @summary Closes the underlying file handle and releases the VFS driver's reference to the underlying stream decoder.
/// @param file_info Internal information relating to the file to close.
public_function void 
OsCloseFile
(
    OS_FILE *file_info
)
{
    if (file_info->Filemap != NULL)
    {
        CloseHandle(file_info->Filemap);
        file_info->Filemap = NULL;
    }
    if (file_info->Filedes != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file_info->Filedes);
        file_info->Filedes = INVALID_HANDLE_VALUE;
    }
}

/// @summary Open a file. Close the file using OsCloseFile().
/// @param fs The file system driver.
/// @param path A NULL-terminated UTF-8 string specifying the virtual file path.
/// @param usage One of OS_FILE_USAGE specifying how the file will be accessed.
/// @param hints One or more of OS_FILE_USAGE_HINTS specifying how to open the file. The file is opened for reading and writing.
/// @param file On return, this structure is populated with file information.
/// @return One of OS_IO_RESULT indicating the result of the operation.
public_function int 
OsOpenFile
(
    OS_FILE_SYSTEM  *fs, 
    char const    *path, 
    int           usage,
    uint32_t      hints, 
    OS_FILE       *file
)
{
    char const  *relpath = NULL;
    int           result = OS_IO_RESULT_SUCCESS;
    if ((result = OsResolveAndOpenFile(fs, path, usage, hints, file, &relpath)) == OS_IO_RESULT_SUCCESS)
    {
        if (hints & OS_FILE_USAGE_HINT_ASYNCHRONOUS)
        {   // associate the file handle with the I/O completion port managed by the AIO driver.
            /*DWORD    asio_result = aio_driver_prepare(driver->AIO, file->Fildes);
            if (!SUCCESS(asio_result))
            {   // could not associate the file handle with the I/O completion port.
                OsCloseFile(file);
                return asio_result;
            }*/
        }
    }
    return result;
}

/// @summary Synchronously reads data from a file opened for manual I/O.
/// @param file The file state returned from OsOpenFile().
/// @param offset The absolute byte offset at which to start reading data.
/// @param buffer The buffer into which data will be written.
/// @param size The maximum number of bytes to read.
/// @param bytes_read On return, this value is set to the number of bytes actually read. This may be less than the number of bytes requested, or 0 at end-of-file.
/// @return One of OS_IO_RESULT.
public_function int 
OsReadFile
(
    OS_FILE      *file, 
    int64_t     offset, 
    void       *buffer, 
    size_t        size, 
    size_t &bytes_read
)
{
    LARGE_INTEGER oldpos;
    LARGE_INTEGER newpos;
    oldpos.QuadPart  = 0;
    newpos.QuadPart  = offset;
    if (!SetFilePointerEx(file->Filedes, newpos, &oldpos, FILE_BEGIN))
    {   // unable to seek to the specified offset.
        bytes_read    = 0;
        file->OSError = GetLastError();
        return OS_IO_RESULT_OSERROR;
    }
    // safely read the requested number of bytes. for very large reads,
    // size > UINT32_MAX, the result is undefined, so possibly split the
    // read up into several sub-reads (though this case is unlikely...)
    bytes_read      = 0;
    uint8_t*bufferp =(uint8_t*) buffer;
    while  (bytes_read < size)
    {
        DWORD nread = 0;
        DWORD rsize = Megabytes(1);
        if (size_t(rsize) > (size - bytes_read))
        {   // read only the amount remaining.
            rsize = (DWORD) (size - bytes_read);
        }
        if (ReadFile(file->Filedes, &bufferp[bytes_read], rsize, &nread, NULL))
        {   // the synchronous read completed successfully.
            bytes_read += nread;
        }
        else
        {   // an error occurred; save the error code.
            file->OSError = GetLastError();
            return OS_IO_RESULT_OSERROR;
        }
    }
    return OS_IO_RESULT_SUCCESS;
}

/// @summary Synchronously writes data to a file opened for manual I/O.
/// @param file The file state returned from OsOpenFile().
/// @param offset The absolute byte offset at which to start writing data.
/// @param buffer The data to be written to the file.
/// @param size The number of bytes to write to the file.
/// @param bytes_written On return, this value is set to the number of bytes actually written to the file.
/// @return One of OS_IO_RESULT.
public_function int 
OsWriteFile
(
    OS_FILE         *file, 
    int64_t        offset, 
    void const    *buffer, 
    size_t           size, 
    size_t &bytes_written
)
{
    LARGE_INTEGER oldpos;
    LARGE_INTEGER newpos;
    oldpos.QuadPart  = 0;
    newpos.QuadPart  = offset;
    if (!SetFilePointerEx(file->Filedes, newpos, &oldpos, FILE_BEGIN))
    {   // unable to seek to the specified offset.
        bytes_written = 0;
        file->OSError = GetLastError();
        return OS_IO_RESULT_OSERROR;
    }
    // safely write the requested number of bytes. for very large writes,
    // size > UINT32_MAX, the result is undefined, so possibly split the
    // write up into several sub-writes (though this case is unlikely...)
    bytes_written  = 0;
    uint8_t const *bufferp = (uint8_t const*) buffer;
    while  (bytes_written  <  size)
    {
        DWORD nwritten = 0;
        DWORD wsize    = Megabytes(1);
        if (size_t(wsize) > (size - bytes_written))
        {   // write only the amount remaining.
            wsize = (DWORD) (size - bytes_written);
        }
        if (WriteFile(file->Filedes, &bufferp[bytes_written], wsize, &nwritten, NULL))
        {   // the synchronous write completed successfully.
            bytes_written += nwritten;
        }
        else
        {   // an error occurred; save the error code.
            file->OSError = GetLastError();
            return OS_IO_RESULT_OSERROR;
        }
    }
    return OS_IO_RESULT_SUCCESS;
}

/// @summary Flush any buffered writes to the file and update file metadata.
/// @param file The file state populated by OsOpenFile().
/// @return One of OS_IO_RESULT.
public_function int 
OsFlushFile
(
    OS_FILE *file
)
{
    if (!FlushFileBuffers(file->Filedes))
    {
        file->OSError = GetLastError();
        return OS_IO_RESULT_OSERROR;
    }
    return OS_IO_RESULT_SUCCESS;
}

// TODO(rlk): mmap I/O synchronous functions
// TODO(rlk): bounded MPSC FIFO for async, prioritized I/O ops => feeds
// TODO(rlk): bounded LPLC priority queue of non-submitted ops => feeds
// TODO(rlk): bounded small array of in-flight async I/O ops
// TODO(rlk): typically, each async I/O op will be for entire set of needed data
// TODO(rlk): no crazy streaming modes or anything - that's all handled by app

// start with VFS implementation. know that we want to support prioritized ASIO.
// each op has callback to run when complete.
// to integrate with task system, allocate task handle for pseudo-task, in ASIO
// completion callback, complete task (on ASIO thread, so must be fast!)

