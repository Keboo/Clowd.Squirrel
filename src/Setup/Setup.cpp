#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <VersionHelpers.h>
#include <string>
#include <fstream>
#include "bundle_marker.h"
#include "simple_zip.h"
#include "platform_util.h"

using namespace std;

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
    // https://docs.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-security
    SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT);
    SetDllDirectory(L"");
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

    // squirrel supports Win7 with ESU Y3, but we can't detect ESU easily.
    if (!IsWindows7SP1OrGreater()) {
        util::show_error_dialog(L"This installer requires Windows 7 SP1 or later and cannot run.");
        return 0;
    }

    wstring myPath = util::get_current_process_path();
    wstring updaterPath = util::get_temp_file_path(L"exe");
    uint8_t* memAddr = 0;

    try {
        // locate bundled package and map to memory
        memAddr = util::mmap_read(util::get_current_process_path(), 0);
        if (!memAddr) {
            throw wstring(L"Unable to memmap current executable. Is there enough available system memory?");
        }

        int64_t packageOffset, packageLength;
        bundle_marker_t::header_offset(&packageOffset, &packageLength);
        uint8_t* pkgStart = memAddr + packageOffset;
        if (packageOffset == 0 || packageLength == 0) {
            throw wstring(L"The embedded package containing the application to install was not found. Please contact the application author.");
        }

        // rough check for sufficient disk space before extracting anything
        // required space is size of compressed nupkg, size of extracted app, 
        // and squirrel overheads (incl temp files). the constant 0.38 is a
        // aggressive estimate on what the compression ratio might be.
        int64_t squirrelOverhead = 50 * 1000 * 1000;
        int64_t requiredSpace = squirrelOverhead + (packageLength * 2) + (int64_t)((double)packageLength / (double)0.38);
        if (!util::check_diskspace(requiredSpace)) {
            throw wstring(L"Insufficient disk space. This application requires at least " + util::pretty_bytes(requiredSpace) + L" free space to be installed.");
        }

        // extract Update.exe and embedded nuget package
        simple_zip zip(pkgStart, (size_t)packageLength);
        zip.extract_updater_to_file(updaterPath);

        // run installer and forward our command line arguments
        wstring cmd = L"\"" + updaterPath + L"\" --setup \"" + myPath + L"\" --setupOffset " + to_wstring(packageOffset) + L" " + pCmdLine;
        util::wexec(cmd.c_str());
    }
    catch (wstring wsx) {
        util::show_error_dialog(L"An error occurred while running setup. " + wsx);
    }
    catch (...) {
        util::show_error_dialog(L"An unknown error occurred while running setup. Please contact the application author.");
    }

    // clean-up resources
    if (memAddr) util::munmap(memAddr);
    DeleteFile(updaterPath.c_str());
    return 0;
}