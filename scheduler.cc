/*/////////////////////////////////////////////////////////////////////////////
/// @summary Test the thread pool and task profiler functionality.
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include <atomic>
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
    OS_MEMORY_ARENA                       arena  = {};  // The memory arena used to store the TASK_DATA instances.
    OS_CPU_INFO                        cpu_info  = {};  // Information about the host CPU configuration.
    OS_TASK_SCHEDULER                 scheduler  = {};  // The task scheduler.
    size_t const               MAIN_THREAD_POOL  = 0;
    size_t const                 IO_THREAD_POOL  = 1;
    size_t const          SCHEDULER_THREAD_POOL  = 2;
    size_t const                TASK_POOL_COUNT  = 3;
    OS_TASK_POOL_INIT pool_init[TASK_POOL_COUNT] = {};
    OS_TASK_SCHEDULER_INIT       scheduler_init  = {};

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    // create the memory arena used to store the TASK_DATA work items.
    if (OsCreateMemoryArena(&arena, Megabytes(64), true, true) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize main memory arena.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }
    if (!OsQueryHostCpuLayout(&cpu_info, &arena))
    {
        OsLayerError("ERROR: %S(%u): Unable to query host CPU layout.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }

    // create three types of task pools - one for the main thread, one for I/O workers, and one for scheduler workers.
    // the main thread can define, but not ever run tasks. it creates very few tasks.
    // the I/O threads can define, but not ever run tasks. they create very few tasks.
    // the scheduler worker threads can define and execute tasks. they may create many tasks.
    pool_init[MAIN_THREAD_POOL].PoolId               = MAIN_THREAD_POOL;
    pool_init[MAIN_THREAD_POOL].PoolUsage            = OS_TASK_POOL_USAGE_FLAG_DEFINE | OS_TASK_POOL_USAGE_FLAG_PUBLISH;
    pool_init[MAIN_THREAD_POOL].PoolCount            = 1;
    pool_init[MAIN_THREAD_POOL].MaxIoRequests        = 0;
    pool_init[MAIN_THREAD_POOL].MaxActiveTasks       = 64;
    pool_init[MAIN_THREAD_POOL].LocalMemorySize      = 0;

    pool_init[IO_THREAD_POOL].PoolId                 = IO_THREAD_POOL;
    pool_init[IO_THREAD_POOL].PoolUsage              = OS_TASK_POOL_USAGE_FLAG_DEFINE | OS_TASK_POOL_USAGE_FLAG_PUBLISH;
    pool_init[IO_THREAD_POOL].PoolCount              = cpu_info.PhysicalCores;
    pool_init[IO_THREAD_POOL].MaxIoRequests          = 0;
    pool_init[IO_THREAD_POOL].MaxActiveTasks         = OS_MIN_TASKS_PER_POOL;
    pool_init[IO_THREAD_POOL].LocalMemorySize        = 0;

    pool_init[SCHEDULER_THREAD_POOL].PoolId          = SCHEDULER_THREAD_POOL;
    pool_init[SCHEDULER_THREAD_POOL].PoolUsage       = OS_TASK_POOL_USAGE_FLAG_DEFINE | OS_TASK_POOL_USAGE_FLAG_EXECUTE | OS_TASK_POOL_USAGE_FLAG_PUBLISH | OS_TASK_POOL_USAGE_FLAG_WORKER;
    pool_init[SCHEDULER_THREAD_POOL].MaxIoRequests   = 512;
    pool_init[SCHEDULER_THREAD_POOL].MaxActiveTasks  = 4096;
    pool_init[SCHEDULER_THREAD_POOL].LocalMemorySize = Megabytes(32);

    // the task scheduler will create and manage its own pool of worker threads.
    scheduler_init.WorkerThreadCount = cpu_info.HardwareThreads;
    scheduler_init.GlobalMemorySize  = Megabytes(256);
    scheduler_init.PoolTypeCount     = TASK_POOL_COUNT;
    scheduler_init.TaskPoolTypes     = pool_init;
    scheduler_init.IoThreadPool      = NULL;
    scheduler_init.TaskContextData   = 0;
    if (OsCreateTaskScheduler(&scheduler, &scheduler_init, &arena, "Task Scheduler") < 0)
    {
        OsLayerError("ERROR: %S(%u): Failed to initialize task scheduler.\n", __FUNCTION__, OsThreadId());
        return -1;
    }

    Sleep(10 * 1000);

    // shut down the task scheduler and kill all worker threads.
    OsDestroyTaskScheduler(&scheduler);

    // clean up everything else.
    OsDeleteMemoryArena(&arena);
    return 0;
}

