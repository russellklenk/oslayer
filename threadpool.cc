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
/// @summary Define the data associated with a simple task.
struct TASK_DATA
{
    uint32_t WorkTime;   /// The number of milliseconds of work to simulate.
};

/*///////////////
//   Globals   //
///////////////*/

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
internal_function int
WorkerInit
(
    OS_WORKER_THREAD *worker
)
{   // TODO(rlk): worker->ThreadArena and worker->ArenaSize can be used to allocate thread-local data.
    // TODO(rlk): worker->PoolContext is set to the application-specific data associated with the thread pool
    // TODO(rlk): worker->ThreadId specifies the 32-bit unsigned integer thread identifier.
    // TODO(rlk): set worker->ThreadArgs to point to thread-local data.
    UNREFERENCED_PARAMETER(worker);
    return OS_WORKER_THREAD_INIT_SUCCESS;
}

internal_function void
WorkerSignal
(
    OS_WORKER_THREAD *worker, 
    uintptr_t         signal, 
    int          wake_reason
)
{
    if (wake_reason == OS_WORKER_THREAD_WAKE_FOR_EXIT)
    {   // TODO(rlk): thread-local data cleanup.
        return;
    }
    if (wake_reason == OS_WORKER_THREAD_WAKE_FOR_ERROR)
    {   // TODO(rlk): not sure what the purpose of this is.
        return;
    }
    if (wake_reason == OS_WORKER_THREAD_WAKE_FOR_SIGNAL)
    {   // TODO(rlk): the thread was woken up to check some piece of data or condition.
        // in this case signal is always zero.
        return;
    }

    // else, this worker thread received an explicit wakeup signal.
    // in this case, signal is always non-zero.
    TASK_DATA *work_item = (TASK_DATA*) signal;
    uint64_t  start_time =  OsTimestampInTicks();
    uint64_t   work_time =  OsMillisecondsToNanoseconds(work_item->WorkTime);
    do
    { /* spin spin spin */
    } while (OsElapsedNanoseconds(start_time, OsTimestampInTicks()) < work_time);

    // mark the work item as having completed.
    std::atomic<uint32_t> *counter = (std::atomic<uint32_t>*) worker->PoolContext;
    counter->fetch_add(1);
}

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
    OS_MEMORY_ARENA         arena = {};  // The memory arena used to store the TASK_DATA instances.
    OS_CPU_INFO          cpu_info = {};  // Information about the host CPU configuration.
    OS_THREAD_POOL           pool = {};  // The worker thread pool.
    OS_THREAD_POOL_INIT pool_init = {};  // Data used to configure the thread pool.
    TASK_DATA          *task_data = NULL;
    std::atomic<uint32_t>   ndone = 0;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    // create the memory arena used to store the TASK_DATA work items.
    if (OsCreateMemoryArena(&arena, Megabytes(2), true, true) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize main memory arena.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }
    if (!OsQueryHostCpuLayout(&cpu_info, &arena))
    {
        OsLayerError("ERROR: %S(%u): Unable to query host CPU layout.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }
    if ((task_data = OsMemoryArenaAllocateArray<TASK_DATA>(&arena, 100)) == NULL)
    {
        OsLayerError("ERROR: %S(%u): Unable to allocate TASK_DATA array.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }

    // initialize the TASK_DATA items to execute for a small, but random number of milliseconds.
    for (size_t i = 0; i < 100; ++i)
    {
        task_data[i].WorkTime = (rand() % 30) + 1;
    }

    // create the thread pool used to execute the tasks.
    pool_init.ThreadInit  = WorkerInit;
    pool_init.ThreadMain  = WorkerSignal;
    pool_init.PoolContext = &ndone; // pass any data you want here.
    pool_init.ThreadCount = 2;      // specify the number of worker threads.
    pool_init.StackSize   = OS_WORKER_THREAD_STACK_DEFAULT;
    pool_init.ArenaSize   = Megabytes(4);
    pool_init.NUMAGroup   = 0;
    if (OsCreateThreadPool(&pool, &pool_init, &arena, "Worker Pool") < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize the worker thread pool.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }

    // at this point, all threads are started and waiting, but cannot execute work items.
    // launch all threads in the pool and have them start waiting for work.
    OsLaunchThreadPool(&pool);

    // submit all work items. worker threads will increment ndone when they complete an item.
    for (size_t i = 0; i < 100; ++i)
    {
        OsSignalWorkerThreads(&pool, (uintptr_t) &task_data[i], 1);
    }

    // busy-wait for all work items to complete.
    do
    {
        Sleep(10);
    } while (ndone.load() != 100);

    OsLayerOutput("STATUS: %S(%u): All work items have completed.\n", __FUNCTION__, GetCurrentThreadId());

    // all work items have completed, terminate the thread pool.
    OsDestroyThreadPool(&pool);

    // clean up everything else.
    OsDeleteMemoryArena(&arena);
    return 0;
}
