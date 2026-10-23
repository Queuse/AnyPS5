#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>
#include <io.h>

extern "C" {
int APS5_VABI fcntl_nid_postfix(int, int, ...);
}

namespace {

constexpr int GuestGetDescriptorFlags = 1;
constexpr int GuestGetFlags = 3;
constexpr int GuestAccessMode = 3;
constexpr int GuestReadOnly = 0;
constexpr int GuestWriteOnly = 1;

int Child(const char* closed) {
    for (int descriptor = 0; descriptor <= 2; ++descriptor) {
        if (closed[descriptor] != '1') continue;
        const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(descriptor));
        if (handle == INVALID_HANDLE_VALUE || handle == reinterpret_cast<HANDLE>(-2)) return 70 + descriptor;
        if (::GetFileType(handle) != FILE_TYPE_CHAR) return 73 + descriptor;
        if (fcntl_nid_postfix(descriptor, GuestGetDescriptorFlags) != 0) return 76 + descriptor;
        const int expected = descriptor == 0 ? GuestReadOnly : GuestWriteOnly;
        if ((fcntl_nid_postfix(descriptor, GuestGetFlags) & GuestAccessMode) != expected) return 79 + descriptor;
    }
    return 0;
}

DWORD RunWithout(const wchar_t* self, bool input, bool output, bool error) {
    std::wstring command = std::wstring(L"\"") + self + L"\" --closed-child " + (input ? L"1" : L"0") + (output ? L"1" : L"0") + (error ? L"1" : L"0");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input ? nullptr : GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output ? nullptr : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = error ? nullptr : GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(self, command.data(), nullptr, nullptr, TRUE, DETACHED_PROCESS, nullptr, nullptr, &startup, &process)) return 0xffffffffu;
    DWORD code = 0xfffffffeu;
    if (WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else TerminateProcess(process.hProcess, code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code;
}

}

int main(int argc, char** argv) {
    if (argc > 2 && std::strcmp(argv[1], "--closed-child") == 0 && std::strlen(argv[2]) == 3) return Child(argv[2]);
    wchar_t self[MAX_PATH];
    if (GetModuleFileNameW(nullptr, self, MAX_PATH) == 0) return 2;
    const struct { bool input, output, error; const char* name; } cases[] = {
        {true, false, false, "stdin"},
        {false, true, false, "stdout"},
        {true, true, true, "all three"},
    };
    for (const auto& test : cases) {
        const DWORD result = RunWithout(self, test.input, test.output, test.error);
        if (result != 0) {
            std::fprintf(stderr, "missing standard handles, %s: child exited with %lu\n", test.name, result);
            return 1;
        }
    }
    std::puts("missing standard handles are opened on NUL");
    return 0;
}
