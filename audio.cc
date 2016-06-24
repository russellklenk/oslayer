/*/////////////////////////////////////////////////////////////////////////////
/// @summary Test the audio device enumeration functionality.
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
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    OS_MEMORY_ARENA             arena = {};
    OS_AUDIO_SYSTEM      audio_system = {};
    OS_AUDIO_DEVICE_LIST  device_list = {};

    // create the memory arena used to store the audio device properties.
    if (OsCreateMemoryArena(&arena, Megabytes(2), true, true) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize main memory arena.\n", __FUNCTION__, GetCurrentThreadId());
        return -1;
    }
    if (OsInitializeAudio(&audio_system) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to initialize the audio system.\n", __FUNCTION__, GetCurrentThreadId());
        return -2;
    }
    if (OsEnumerateAudioDevices(&device_list, &audio_system, &arena) < 0)
    {
        OsLayerError("ERROR: %S(%u): Unable to enumerate audio devices.\n", __FUNCTION__, GetCurrentThreadId());
        return -3;
    }
    OsLayerOutput("OUTPUT DEVICES:\n");
    for (size_t i = 0, n = device_list.OutputDeviceCount; i < n; ++i)
    {
        OsLayerOutput("Id:   %s\nName: %s\n", device_list.OutputDeviceId[i], device_list.OutputDeviceName[i]);
    }
    OsLayerOutput("\n");
    OsLayerOutput("CAPTURE DEVICES:\n");
    for (size_t i = 0, n = device_list.CaptureDeviceCount; i < n; ++i)
    {
        OsLayerOutput("Id:   %s\nName: %s\n", device_list.CaptureDeviceId[i], device_list.CaptureDeviceName[i]);
    }
    OsLayerOutput("\n");
    return 0;
}

