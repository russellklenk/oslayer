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
                return false;                                                  \
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
                return false;                                                  \
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
                return false;                                                  \
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
            if (((vkdevice)->fname = (PFN_##fname) (vkinstance)->vkGetDeviceProcAddr((vkdevice)->LogicalDevice, #fname)) == NULL) { \
                OsLayerError("ERROR: %S(%u): Unable to resolve Vulkan device entry point \"%S\".\n", __FUNCTION__, GetCurrentThreadId(), #fname); \
                return false;                                                  \
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
    #include <Shellapi.h>
    #include <strsafe.h>
    #include <XInput.h>
    #include <intrin.h>

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
struct OS_KEYBOARD_EVENTS;
struct OS_POINTER_EVENTS;
struct OS_GAMEPAD_EVENTS;
struct OS_INPUT_EVENTS;
struct OS_INPUT_SYSTEM;
struct OS_DISPLAY;

struct OS_TASK_PROFILER;
struct OS_TASK_PROFILER_SPAN;

struct OS_VULKAN_LOADER;
struct OS_VULKAN_DEVICE;
struct OS_VULKAN_INSTANCE;
struct OS_VULKAN_PHYSICAL_DEVICE;

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

/// @summary Defines the data associated with a display output.
struct OS_DISPLAY
{
    DWORD               Ordinal;                     /// The unique display ordinal number.
    HMONITOR            Monitor;                     /// The operating system monitor handle.
    int                 DisplayX;                    /// The x-coordinate of the upper-left corner of the display, in virtual display coordinates.
    int                 DisplayY;                    /// The y-coordinate of the upper-left corner of the display, in virtual display coordinates.
    int                 DisplayWidth;                /// The width of the display, in pixels.
    int                 DisplayHeight;               /// The height of the display, in pixels.
    DEVMODE             DisplayMode;                 /// The active display settings.
    DISPLAY_DEVICE      DisplayInfo;                 /// Information uniquely identifying the display to the operating system.
};

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
    VkPhysicalDeviceMemoryProperties  Memory;                                        /// Information about the memory access features of the physical device.
    size_t                            QueueFamilyCount;                              /// The number of command queue families exposed by the device.
    VkQueueFamilyProperties          *QueueFamily;                                   /// An array of QueueFamilyCount VkQueueFamiliyProperties describing command queue attributes.
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

/// @summary Define the set of display orientations.
enum OS_DISPLAY_ORIENTATION          : int
{
    OS_DISPLAY_ORIENTATION_LANDSCAPE = 0,            /// The display orientation is landscape.
    OS_DISPLAY_ORIENTATION_PORTRAIT  = 1,            /// The display orientation is portrait.
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
/// @param vkloader The OS_VULKAN_LOADER instance, with the LoaderHandle field set.
/// @return true if all exported functions were resolved successfully.
internal_function bool
OsVulkanLoaderResolveIcdFunctions
(
    OS_VULKAN_LOADER *vkloader
)
{
    OS_LAYER_RESOLVE_VULKAN_ICD_FUNCTION(vkloader, vkGetInstanceProcAddr);
    return true;
}

/// @summary Resolve global functions exported by a Vulkan ICD.
/// @param vkloader The OS_VULKAN_LOADER instance, with the LoaderHandle field set.
/// @return true if all global functions were resolved successfully.
internal_function bool
OsVulkanLoaderResolveGlobalFunctions
(
    OS_VULKAN_LOADER *vkloader
)
{
    OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(vkloader, vkCreateInstance);
    OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(vkloader, vkEnumerateInstanceLayerProperties);
    OS_LAYER_RESOLVE_VULKAN_GLOBAL_FUNCTION(vkloader, vkEnumerateInstanceExtensionProperties);
    return true;
}

/// @summary Resolve instance-level functions exported by a Vulkan ICD.
/// @param vkloader The OS_VULKAN_LOADER object, with the vkGetInstanceProcAddr entry point set.
/// @param vkinstance The OS_VULKAN_INSTANCE object, with the InstanceHandle field set.
/// @param create_info The VkInstanceCreateInfo being used to initialize the instance. Used here to resolve instance-level function pointers for extensions.
/// @return true if all instance-level functions were resolved successfully.
internal_function bool
OsVulkanLoaderResolveInstanceFunctions
(
    OS_VULKAN_LOADER               *vkloader,
    OS_VULKAN_INSTANCE           *vkinstance,
    VkInstanceCreateInfo  const *create_info 
)
{
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkCreateDevice);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkDestroyInstance);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkEnumeratePhysicalDevices);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkGetPhysicalDeviceFeatures);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkGetPhysicalDeviceProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkGetPhysicalDeviceMemoryProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkGetPhysicalDeviceQueueFamilyProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkEnumerateDeviceLayerProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkEnumerateDeviceExtensionProperties);
    OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkGetDeviceProcAddr);
    if (OsVulkanInstanceExtensionEnabled(VK_KHR_SURFACE_EXTENSION_NAME, create_info))
    {   // resolve entry points for VK_KHR_surface.
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkDestroySurfaceKHR);
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkGetPhysicalDeviceSurfaceSupportKHR);
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkGetPhysicalDeviceSurfaceFormatsKHR);
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkGetPhysicalDeviceSurfacePresentModesKHR);
    }
    if (OsVulkanInstanceExtensionEnabled(VK_KHR_WIN32_SURFACE_EXTENSION_NAME, create_info))
    {   // resolve entry points for VK_KHR_win32_surface.
        OS_LAYER_RESOLVE_VULKAN_INSTANCE_FUNCTION(vkinstance, vkloader, vkCreateWin32SurfaceKHR);
    }
}

/// @summary Resolve device-level functions exported by the Vulkan ICD.
/// @param vkinstance The OS_VULKAN_INSTANCE object, with the vkGetDeviceProcAddr entry point set.
/// @param vkdevice The OS_VULKAN_DEVICE object, with the LogicalDevice field set.
/// @param create_info The VkDeviceCreateInfo being used to initialize the logical device. Used here to resolve device-level function pointers for extensions.
/// @return true if all device-level functions were resolved successfully.
internal_function bool
OsVulkanLoaderResolveDeviceFunctions
(
    OS_VULKAN_INSTANCE        *vkinstance,
    OS_VULKAN_DEVICE            *vkdevice, 
    VkDeviceCreateInfo const *create_info
)
{
    UNREFERENCED_PARAMETER(create_info);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(vkdevice, vkinstance, vkDestroyDevice);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(vkdevice, vkinstance, vkGetDeviceQueue);
    OS_LAYER_RESOLVE_VULKAN_DEVICE_FUNCTION(vkdevice, vkinstance, vkDeviceWaitIdle);
}

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
public_function inline T*
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
        vkinstance->vkGetPhysicalDeviceMemoryProperties(handle, &dev->Memory);
        vkinstance->vkGetPhysicalDeviceQueueFamilyProperties(handle, &family_count, NULL);
        if (family_count != 0)
        {   // allocate storage for and retrieve queue family information.
            if ((dev->QueueFamily = OsMemoryArenaAllocateArray<VkQueueFamilyProperties>(arena, family_count)) == NULL)
            {
                OsLayerError("ERROR: %S(%u): Unable to allocate memory for Vulkan physical device queue family properties.\n", __FUNCTION__, GetCurrentThreadId());
                ldresult = OS_VULKAN_LOADER_RESULT_NOMEMORY;
                goto cleanup_and_fail;
            }
            dev->QueueFamilyCount = family_count;
            vkinstance->vkGetPhysicalDeviceQueueFamilyProperties(handle, &family_count, dev->QueueFamily);
        }
        else
        {   // don't allocate any memory for zero-size arrays.
            dev->QueueFamilyCount = 0;
            dev->QueueFamily = NULL;
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
    ZeroMemory(vkinstance, sizeof(OS_VULKAN_INSTANCE));
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

    // ...
    return OS_VULKAN_LOADER_RESULT_SUCCESS;

cleanup_and_fail:
    ZeroMemory(vkdevice, sizeof(OS_VULKAN_DEVICE));
    OsMemoryArenaResetToMarker(arena, marker);
    return ldresult;
}

// TODO(rlk): Async disk I/O system.
// TODO(rlk): Vulkan display driver.
// TODO(rlk): OpenGL display driver.
// TODO(rlk): Network driver.
// TODO(rlk): Low-latency audio driver.

