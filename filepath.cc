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
    L"C:\\.vim",
    L"\\\\.\\SomeDevice", 
    L"\\\\?\\C:", 
    L"\\\\?\\C:\\", 
    L"\\\\?\\C:\\foo", 
    L"\\\\?\\C:\\foo\\", 
    L"\\\\?\\C:\\foo.a", 
    L"\\\\?\\C:\\foo\\bar.a.b",
    L"\\\\?\\C:\\.vim",
    L"\\\\?\\.\\SomeDevice",
    L"\\\\UNC", 
    L"\\\\UNC\\", 
    L"\\\\UNC\\foo", 
    L"\\\\UNC\\foo\\", 
    L"\\\\UNC\\foo.a", 
    L"\\\\UNC\\foo\\bar.a.b",
    L"\\\\UNC\\.vim",
    L"\\\\?\\UNC", 
    L"\\\\?\\UNC\\", 
    L"\\\\?\\UNC\\foo", 
    L"\\\\?\\UNC\\foo\\", 
    L"\\\\?\\UNC\\foo.a", 
    L"\\\\?\\UNC\\foo\\bar.a.b", 
    L"\\\\?\\UNC\\.vim", 
    L"\\", 
    L"foo", 
    L"foo\\",
    L"foo\\bar",
    L"foo\\bar.a", 
    L"foo\\bar.a.b",
    L"foo\\.vim", 
    L".", 
    L".vim", 
    L"..", 
    L"..\\", 
    L".\\", 
    L".\\foo", 
    L".\\foo.a", 
    L".\\foo\\.vim", 
    L".\\foo\\bar.a", 
    L".\\foo\\bar.a.b",
    NULL
};

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
internal_function void
PrintPart
(
    WCHAR const *header, 
    WCHAR const    *beg, 
    WCHAR const    *end
)
{
    wprintf(L"%s: ", header);
    while (beg != end)
    {
        wprintf(L"%c", *beg++);
    }
    wprintf(L"\n");
}

internal_function void
PrintPathParts
(
    WCHAR const *inpp,
    OS_PATH_PARTS *pp
)
{
    wprintf(L"INPP: %s\n", inpp);
    PrintPart(L"ROOT", pp->Root, pp->RootEnd);
    PrintPart(L"PATH", pp->Path, pp->PathEnd);
    PrintPart(L"FNAM", pp->Filename, pp->FilenameEnd);
    PrintPart(L"FEXT", pp->Extension, pp->ExtensionEnd);
    wprintf(L"FLAG: ");
    if (pp->PathFlags & OS_PATH_FLAG_ABSOLUTE) wprintf(L"A");
    if (pp->PathFlags & OS_PATH_FLAG_RELATIVE) wprintf(L"R");
    if (pp->PathFlags & OS_PATH_FLAG_LONG) wprintf(L"L");
    if (pp->PathFlags & OS_PATH_FLAG_UNC) wprintf(L"U");
    if (pp->PathFlags & OS_PATH_FLAG_DEVICE) wprintf(L"D");
    if (pp->PathFlags & OS_PATH_FLAG_ROOT) wprintf(L"r");
    if (pp->PathFlags & OS_PATH_FLAG_PATH) wprintf(L"p");
    if (pp->PathFlags & OS_PATH_FLAG_FILENAME) wprintf(L"f");
    if (pp->PathFlags & OS_PATH_FLAG_EXTENSION) wprintf(L"e");
    wprintf(L"\n\n");
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
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    OS_MEMORY_ARENA arena = {};
    OS_FSIC_ALLOCATOR alloc = {};
    OS_FILE_INFO_CHUNK *chunk = NULL;
    HANDLE dir = INVALID_HANDLE_VALUE;
    size_t nfiles = 0;

    // create the memory arena used to store temporary memory.
    if (OsCreateMemoryArena(&arena, Megabytes(2), true, true) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize main memory arena.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }
    OsCreateNativeDirectory(L"C:\\git\\oslayer\\build", NULL);
    // test the file system enumeration code.
    OsInitFileSystemInfoChunkAllocator(&alloc, &arena);
    if (OsOpenNativeDirectory(L"C:\\git\\oslayer", dir) < 0)
    {
    }
    chunk = OsNativeDirectoryFindFiles(dir, L"*", true, nfiles, &alloc);
    OsCloseNativeDirectory(dir);
    OsFreeFileInfoChunkList(&alloc, chunk);
    OsMemoryArenaReset(&arena);
    // TODO(rlk): need to be able to destroy allocator.
    {
        size_t index = 0;
        while (InputPaths[index] != NULL)
        {
            WCHAR *outpath  = NULL;
            OS_PATH_PARTS p = {};

            OsMemoryArenaReset(&arena);
            outpath  = OsMemoryArenaAllocateArray<WCHAR>(&arena, 32768);
            StringCchCopyW(outpath, 32768, InputPaths[index]);
            OsNativePathParse(outpath, NULL, &p);
            PrintPathParts(InputPaths[index],&p);
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


