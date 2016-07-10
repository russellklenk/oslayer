/*/////////////////////////////////////////////////////////////////////////////
/// @summary Test the file path parsing functionality, which is a breeding 
/// ground for buffer overruns and off-by-one errors.
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
static WCHAR const *InputPaths[] = 
{
    L"C:", 
    L"C:\\", 
    L"C:\\foo", 
    L"C:\\foo\\", 
    L"C:\\foo.a", 
    L"C:\\foo\\bar.a.b", 
    L"\\\\?\\C:", 
    L"\\\\?\\C:\\", 
    L"\\\\?\\C:\\foo", 
    L"\\\\?\\C:\\foo\\", 
    L"\\\\?\\C:\\foo.a", 
    L"\\\\?\\C:\\foo\\bar.a.b", 
    L"\\\\UNC", 
    L"\\\\UNC\\", 
    L"\\\\UNC\\foo", 
    L"\\\\UNC\\foo\\", 
    L"\\\\UNC\\foo.a", 
    L"\\\\UNC\\foo\\bar.a.b", 
    L"\\\\?\\UNC", 
    L"\\\\?\\UNC\\", 
    L"\\\\?\\UNC\\foo", 
    L"\\\\?\\UNC\\foo\\", 
    L"\\\\?\\UNC\\foo.a", 
    L"\\\\?\\UNC\\foo\\bar.a.b", 
    NULL
};

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
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    OS_MEMORY_ARENA arena = {};

    // create the memory arena used to store temporary memory.
    if (OsCreateMemoryArena(&arena, Megabytes(2), true, true) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize main memory arena.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }
    {
        size_t index = 0;
        while (InputPaths[index] != NULL)
        {
            WCHAR *outend   = NULL;
            WCHAR *outpath  = NULL;
            size_t outsize  = 32768 * sizeof(WCHAR);
            size_t outbytes = 0;
            size_t outchars = 0;

            OsMemoryArenaReset(&arena);
            outpath  = OsMemoryArenaAllocateArray<WCHAR>(&arena, 32768);
            StringCchCopyW(outpath, 32768, InputPaths[index]);
            outchars = OsNativePathChangeExtension(outpath, outsize, &outend, outbytes, L"");
            OsLayerOutput("INP: %s\nOUT: %s\n\n", InputPaths[index], outpath);
            ++index;
        }
    }
    {
        size_t index = 0;
        while (InputPaths[index] != NULL)
        {
            WCHAR *outend   = NULL;
            WCHAR *outpath  = NULL;
            size_t outsize  = 32768 * sizeof(WCHAR);
            size_t outbytes = 0;
            size_t outchars = 0;

            OsMemoryArenaReset(&arena);
            outpath  = OsMemoryArenaAllocateArray<WCHAR>(&arena, 32768);
            StringCchCopyW(outpath, 32768, InputPaths[index]);
            outchars = OsNativePathChangeExtension(outpath, outsize, &outend, outbytes, L".axx.bxx");
            OsLayerOutput("INP: %s\nOUT: %s\n\n", InputPaths[index], outpath);
            ++index;
        }
    }
    {
        size_t index = 0;
        while (InputPaths[index] != NULL)
        {
            WCHAR *outend   = NULL;
            WCHAR *outpath  = NULL;
            size_t outsize  = 32768 * sizeof(WCHAR);
            size_t outbytes = 0;
            size_t outchars = 0;

            OsMemoryArenaReset(&arena);
            outpath  = OsMemoryArenaAllocateArray<WCHAR>(&arena, 32768);
            StringCchCopyW(outpath, 32768, InputPaths[index]);
            outchars = OsNativePathAppend(outpath, outsize, &outend, outbytes, L"car");
            OsLayerOutput("INP: %s\nOUT: %s\n\n", InputPaths[index], outpath);
            ++index;
        }
    }
    {
        size_t index = 0;
        while (InputPaths[index] != NULL)
        {
            WCHAR *outend   = NULL;
            WCHAR *outpath  = NULL;
            size_t outsize  = 32768 * sizeof(WCHAR);
            size_t outbytes = 0;
            size_t outchars = 0;

            OsMemoryArenaReset(&arena);
            outpath  = OsMemoryArenaAllocateArray<WCHAR>(&arena, 32768);
            StringCchCopyW(outpath, 32768, InputPaths[index]);
            outchars = OsNativePathAppendExtension(outpath, outsize, &outend, outbytes, L"car");
            OsLayerOutput("INP: %s\nOUT: %s\n\n", InputPaths[index], outpath);
            ++index;
        }
    }
    return 0;
}


