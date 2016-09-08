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
struct TASK_ID_AND_THREAD
{
    os_task_id_t        TaskId;         /// The task identifier.
    uint32_t            ThreadId;       /// The operating system identifier of the thread that executed the task.
};

struct WRITE_TASK_ID_ARGS
{
    TASK_ID_AND_THREAD *IdTable;        /// The task ID table, allocated in global memory.
    uint32_t            TaskIndex;      /// The zero-based index of the child task. The task should write its ID to this slot.
};

struct WRITE_TASK_ID_CHUNK_ARGS
{
    TASK_ID_AND_THREAD *Expect;         /// The list of expected task IDs.
    TASK_ID_AND_THREAD *Result;         /// The list of received task IDs.
    uint32_t            StartIndex;     /// The zero-based index of the first entry to write in the ID table.
    uint32_t            ItemCount;      /// The total number of entries to write in the ID table.
};

struct EMPTY_CHILD_TEST_STATE
{
    TASK_ID_AND_THREAD *Expect;         /// The list of expected task IDs.
    TASK_ID_AND_THREAD *Result;         /// The list of received task IDs.
    uint32_t            ChildCount;     /// The number of child tasks to spawn.
};
/// @summary Define the data passed from the test harness to the root task of the test.
struct TEST_TASK_ARGS
{
    uintptr_t           TestState;      /// State data for the test, allocated in global memory.
    bool               *TestSucceeded;  /// A location to store the result of the test.
};
#define TEST_SUCCEEDED(_test_task_args) *(_test_task_args)->TestSucceeded = true
#define TEST_FAILED(_test_task_args)  *(_test_task_args)->TestSucceeded = false

/// @summary Define the signature for the callback invoked before the root task for a test is created.
/// @param taskenv The OS_TASK_ENVIRONMENT for the main thread.
/// @param test_state On return, set this value to test state data to be passed to the shutdown function.
/// @return Zero if the test initialization completes successfully, or -1 if an error occurs.
typedef int  (*TEST_INITFUNC)(OS_TASK_ENVIRONMENT *taskenv, uintptr_t *test_state);

/// @summary Define the signature for the callback invoked when a test completes.
/// @param taskenv The OS_TASK_ENVIRONMENT for the main thread.
/// @param test_args The arguments passed to the root task of the test harness.
/// @return true if the test executed successfully.
typedef bool (*TEST_SHUTFUNC)(OS_TASK_ENVIRONMENT *taskenv, TEST_TASK_ARGS *test_args);

/// @summary Ensure that thread profiler events get emitted when a test starts and stops.
struct TEST_SCOPE
{
    OS_TASK_ENVIRONMENT *Env;
    char const     *TestName;
    uint64_t        StartTime;
    TEST_SCOPE(char const *test_name, OS_TASK_ENVIRONMENT *taskenv) : Env(taskenv), TestName(test_name)
    {
        StartTime = OsTimestampInTicks();
        OsTaskEvent(Env, "LAUNCH: %S", TestName);
    }
   ~TEST_SCOPE(void)
    {
        uint64_t EndTime   = OsTimestampInTicks();
        uint64_t ElapsedNs = OsElapsedNanoseconds(StartTime, EndTime);
        uint32_t ElapsedMs = OsNanosecondsToWholeMilliseconds(ElapsedNs);
        OsTaskEvent(Env, "FINISH: %S %ums (%I64uns)", TestName, ElapsedMs, ElapsedNs);
    }
};

/*///////////////
//   Globals   //
///////////////*/

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
/// @summary Execute a test using the task scheduler.
/// @param test_name A zero-terminated string specifying a human-readable name for the test.
/// @param taskenv The OS_TASK_ENVIRONMENT for the main thread.
/// @param test_main The test task entry point.
/// @param test_init The function to call before the root task is spawned to set up any global state.
/// @param test_shutdown The function to call after all tasks have completed to examing 
/// @return true if the test was successful, or false if the test failed.
internal_function bool
ParallelTest
(
    char const          *test_name,
    OS_TASK_ENVIRONMENT   *taskenv,
    OS_TASK_ENTRYPOINT   test_main, 
    TEST_INITFUNC        test_init=NULL, 
    TEST_SHUTFUNC    test_shutdown=NULL
)
{
    TEST_SCOPE          test(test_name, taskenv);
    uintptr_t     test_state = 0;
    TEST_TASK_ARGS      args = {};
    os_task_id_t   root_task = OS_INVALID_TASK_ID;
    OS_TASK_FENCE fence_done = {};
    bool         did_succeed = false;

    // reset the memory arena in preparation for the test run.
    OsMemoryArenaReset(taskenv->GlobalMemory);

    // perform global initialization for the test. this may allocate global memory.
    if (test_init && test_init(taskenv, &test_state) < 0)
    {
        OsLayerError("FAILED: Initialization for test failed.\n");
        return false;
    }
    args.TestState     = test_state;
    args.TestSucceeded =&did_succeed;

    // define the root task for the test scenario.
    // the test tasks to execute should be defined as children of this task.
    if ((root_task = OsDefineTask(taskenv, test_main, &args)) == OS_INVALID_TASK_ID)
    {
        OsLayerError("FAILED: Unable to create root task (%d).\n", OsGetTaskPoolError(taskenv));
        goto test_cleanup;
    }
    if (OsCreateTaskFence(taskenv, &fence_done, &root_task, 1) == OS_INVALID_TASK_ID)
    {
        OsLayerError("FAILED: Unable to create fence (%d).\n", OsGetTaskPoolError(taskenv));
        goto test_cleanup;
    }
    // the root task cannot possibly complete until it is fully-defined,
    // though it may have started (and possibly finished) executing.
    OsFinishTaskDefinition(taskenv, root_task);

    // block the main thread until all work completes.
    OsWaitTaskFence(&fence_done);
    OsDestroyTaskFence(&fence_done);

test_cleanup:
    if (test_shutdown)
    {
        did_succeed = test_shutdown(taskenv, &args);
    }
    OsLayerError("STATUS: Finished test \"%S\" (%S).\n", test_name, did_succeed ? "SUCCEEDED" : "FAILED");
    return did_succeed;
}

/// @summary Implement a no-op test initialization function. Normally, this would initialize the global memory for storing test results.
/// @param taskenv The OS_TASK_ENVIRONMENT for the main thread.
/// @param test_state On return, set this value to test state data to be passed to the shutdown function.
/// @return Zero if initialization is successful, or -1 if initialization failed.
internal_function int
EmptyInit
(
    OS_TASK_ENVIRONMENT *taskenv, 
    uintptr_t        *test_state
)
{
    UNREFERENCED_PARAMETER(taskenv);
   *test_state = 0;
    return 0;
}

/// @summary Implement a no-op test shutdown function. Normally, this would be used to analyze the test results after all tasks finish running.
/// @param taskenv The OS_TASK_ENVIRONMENT for the main thread.
/// @param test_args The arguments passed to the root task of the test harness.
/// @return true if the test was successful, or false if the test failed.
internal_function bool
EmptyShutdown
(
    OS_TASK_ENVIRONMENT *taskenv,
    TEST_TASK_ARGS         *args 
)
{
    UNREFERENCED_PARAMETER(taskenv);
    return *args->TestSucceeded;
}

/// @summary Implement the simplest possible test case - a test that does nothing.
/// @param task_id The unique identifier of the task, returned to the application when the task was defined.
/// @param task_args A pointer to the parameter data supplied with the task. This pointer is always valid.
/// @param taskenv The execution environment for the task, providing access to local and global memory.
internal_function void
EmptyTest
(
    os_task_id_t         task_id, 
    void              *task_args, 
    OS_TASK_ENVIRONMENT *taskenv
)
{
    OS_PROFILE_TASK(task_id, taskenv);
    {
        TEST_TASK_ARGS *args = (TEST_TASK_ARGS*) task_args;
        TEST_SUCCEEDED(args);
    }
}

/// @summary A very simple task that writes its task ID out to global memory.
/// @param task_id The unique identifier of the task, returned to the application when the task was defined.
/// @param task_args A pointer to the parameter data supplied with the task. This pointer is always valid.
/// @param taskenv The execution environment for the task, providing access to local and global memory.
internal_function void
WriteTaskId
(
    os_task_id_t         task_id, 
    void              *task_args, 
    OS_TASK_ENVIRONMENT *taskenv
)
{
    //TASK_SCOPE this_task(__FUNCTION__, task_id, environment);
    {
        WRITE_TASK_ID_ARGS    *args = (WRITE_TASK_ID_ARGS*) task_args;
        args->IdTable[args->TaskIndex].TaskId = task_id;
        args->IdTable[args->TaskIndex].ThreadId = taskenv->ThreadId;
    }
}

/// @summary A parent task that spawns a contiguous chunk of child tasks.
/// @param task_id The unique identifier of the task, returned to the application when the task was defined.
/// @param task_args A pointer to the parameter data supplied with the task. This pointer is always valid.
/// @param taskenv The execution environment for the task, providing access to local and global memory.
internal_function void
WriteTaskIdChunk
(
    os_task_id_t         task_id, 
    void              *task_args, 
    OS_TASK_ENVIRONMENT *taskenv
)
{
    OS_PROFILE_TASK(task_id, taskenv);
    {
        WRITE_TASK_ID_CHUNK_ARGS *args =(WRITE_TASK_ID_CHUNK_ARGS*) task_args;
        TASK_ID_AND_THREAD     *expect = args->Expect;
        TASK_ID_AND_THREAD     *result = args->Result;
        uint32_t                 start = args->StartIndex;
        for (uint32_t i = 0, n = args->ItemCount; i < n; ++i)
        {
            WRITE_TASK_ID_ARGS child_args = {result, start + i};
            if ((expect[start + i].TaskId = OsSpawnChildTask(taskenv, WriteTaskId, &child_args, task_id)) == OS_INVALID_TASK_ID)
            {
                OsLayerError("ERROR: %S(%u): Failed to spawn WriteTaskId child for %u.\n:", __FUNCTION__, taskenv->ThreadId, start+i);
                return;
            }
        }
    }
}
    
/// @summary Initialize the global memory for storing test results.
/// @param taskenv The OS_TASK_ENVIRONMENT for the main thread.
/// @param test_state On return, set this value to test state data to be passed to the shutdown function.
/// @return Zero if initialization is successful, or -1 if initialization failed.
internal_function int
EmptyChildTestInit
(
    OS_TASK_ENVIRONMENT *taskenv, 
    uintptr_t        *test_state
)
{
    uint32_t const               N = 65000;
    os_arena_marker_t       marker = OsMemoryArenaMark(taskenv->GlobalMemory);
    EMPTY_CHILD_TEST_STATE  *state = OsMemoryArenaAllocate<EMPTY_CHILD_TEST_STATE >(taskenv->GlobalMemory);
    TASK_ID_AND_THREAD     *expect = OsMemoryArenaAllocateArray<TASK_ID_AND_THREAD>(taskenv->GlobalMemory, N);
    TASK_ID_AND_THREAD     *result = OsMemoryArenaAllocateArray<TASK_ID_AND_THREAD>(taskenv->GlobalMemory, N);
    if (state == NULL || expect == NULL || result == NULL)
    {
        OsLayerError("ERROR: %S(%u): Failed to allocate global test state.\n", __FUNCTION__, OsThreadId());
        OsMemoryArenaResetToMarker(taskenv->GlobalMemory, marker);
        return -1;
    }
    OsZeroMemory(state , sizeof(EMPTY_CHILD_TEST_STATE));
    OsZeroMemory(expect, sizeof(TASK_ID_AND_THREAD) * N);
    OsZeroMemory(result, sizeof(TASK_ID_AND_THREAD) * N);
    state->Expect      = expect;
    state->Result      = result;
    state->ChildCount  = N;
   *test_state = (uintptr_t) state;
    return 0;
}

/// @summary Analyze the test results after all tasks finish running.
/// @param taskenv The OS_TASK_ENVIRONMENT for the main thread.
/// @param test_args The arguments passed to the root task of the test harness.
/// @return true if the test was successful, or false if the test failed.
internal_function bool
EmptyChildTestShutdown
(
    OS_TASK_ENVIRONMENT *taskenv,
    TEST_TASK_ARGS         *args
)
{
    UNREFERENCED_PARAMETER(taskenv);
    EMPTY_CHILD_TEST_STATE  *state = (EMPTY_CHILD_TEST_STATE*) args->TestState;
    bool mismatch   = false;
    for (uint32_t i = 0, n = state->ChildCount; i < n; ++i)
    {
        if (state->Expect[i].TaskId != state->Result[i].TaskId)
        {
            mismatch = true;
            break;
        }
    }
    if (mismatch)
    {
        TEST_FAILED(args);
    }
    else
    {
        TEST_SUCCEEDED(args);
    }
    return mismatch ? false : true;
}

/// @summary Test execution of child tasks. This root task spawns many child tasks.
/// @param task_id The unique identifier of the task, returned to the application when the task was defined.
/// @param task_args A pointer to the parameter data supplied with the task. This pointer is always valid.
/// @param taskenv The execution environment for the task, providing access to local and global memory.
internal_function void
EmptyChildTest
(
    os_task_id_t         task_id, 
    void              *task_args, 
    OS_TASK_ENVIRONMENT *taskenv
)
{
    OS_PROFILE_TASK(task_id, taskenv);
    {
        TEST_TASK_ARGS        *args = (TEST_TASK_ARGS*) task_args;
        EMPTY_CHILD_TEST_STATE  *st = (EMPTY_CHILD_TEST_STATE*) args->TestState;
        TASK_ID_AND_THREAD  *expect =  st->Expect;
        TASK_ID_AND_THREAD  *result =  st->Result;
        uint32_t            threads = (uint32_t) 7; //taskenv->HostCpuInfo->PhysicalCores;
        uint32_t              count =  st->ChildCount / threads;
        uint32_t              extra =  st->ChildCount % threads;
        
        for (uint32_t i = 0, n = threads; i < n; ++i)
        {
            WRITE_TASK_ID_CHUNK_ARGS child_args;
            child_args.Expect      = expect;
            child_args.Result      = result;
            child_args.StartIndex  = count * i;
            child_args.ItemCount   = count;
            if (i == (n - 1))
            {   // if this is the last chunk, include any extra items.
                child_args.ItemCount += extra;
            }
            if (OsSpawnChildTask(taskenv, WriteTaskIdChunk, &child_args, task_id) == OS_INVALID_TASK_ID)
            {
                TEST_FAILED(args);
            }
            OsPublishTasks(taskenv, 1);
        }
    }
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
    OS_MEMORY_ARENA                       arena  = {};  // The memory arena used to store the TASK_DATA instances.
    OS_CPU_INFO                        cpu_info  = {};  // Information about the host CPU configuration.
    OS_TASK_SCHEDULER                 scheduler  = {};  // The task scheduler.
    OS_TASK_ENVIRONMENT                 rootenv  = {};  // The OS_TASK_ENVIRONMENT for the main thread.
    size_t const               MAIN_THREAD_POOL  = 0;
    size_t const                 IO_THREAD_POOL  = 1;
    size_t const          SCHEDULER_THREAD_POOL  = 2;
    size_t const                TASK_POOL_COUNT  = 3;
    OS_TASK_POOL_INIT pool_init[TASK_POOL_COUNT] = {};
    OS_TASK_SCHEDULER_INIT       scheduler_init  = {};

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    // create the memory arena used to store the TASK_DATA work items.
    if (OsCreateMemoryArena(&arena, Megabytes(128), true, true) < 0)
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
    pool_init[SCHEDULER_THREAD_POOL].PoolCount       = 7;//cpu_info.PhysicalCores;
    pool_init[SCHEDULER_THREAD_POOL].MaxIoRequests   = 512;
    pool_init[SCHEDULER_THREAD_POOL].MaxActiveTasks  = OS_MAX_TASKS_PER_POOL;
    pool_init[SCHEDULER_THREAD_POOL].LocalMemorySize = Megabytes(32);

    // the task scheduler will create and manage its own pool of worker threads.
    scheduler_init.WorkerThreadCount = 7; //cpu_info.PhysicalCores;
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
    if (OsAllocateTaskPool(&rootenv, &scheduler, MAIN_THREAD_POOL, OsThreadId()) < 0)
    {
        OsLayerError("ERROR: %S(%u): Failed to allocate main thread task pool.\n", __FUNCTION__, OsThreadId());
        return -1;
    }

    ParallelTest("EmptyTest", &rootenv, EmptyTest, EmptyInit, EmptyShutdown);
    ParallelTest("EmptyChildTest", &rootenv, EmptyChildTest, EmptyChildTestInit, EmptyChildTestShutdown);

    // shut down the task scheduler and kill all worker threads.
    OsDestroyTaskScheduler(&scheduler);

    // clean up everything else.
    OsDeleteMemoryArena(&arena);
    return 0;
}

