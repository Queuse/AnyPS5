#include "prx/libc/include/FileStream.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <windows.h>
#include <io.h>

extern "C" {
_invalid_parameter_handler _set_thread_local_invalid_parameter_handler(_invalid_parameter_handler);
int APS5_VABI fputc_nid_postfix(int, FileStream*);
int APS5_VABI fflush_nid_postfix(FileStream*);
}

namespace {
int invalidCalls = 0;

void InvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, std::uintptr_t) {
    ++invalidCalls;
}
}

int main() {
    HANDLE report = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &report, 0, FALSE, DUPLICATE_SAME_ACCESS)) return 1;
    const auto finish = [&](int code, const std::string& message) {
        DWORD written = 0;
        WriteFile(report, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
        CloseHandle(report);
        return code;
    };
    const DWORD size = GetEnvironmentVariableW(L"ANYPS5_TEST_LIBC", nullptr, 0);
    if (size == 0) return finish(2, "missing ANYPS5_TEST_LIBC\n");
    std::wstring library(size, L'\0');
    if (GetEnvironmentVariableW(L"ANYPS5_TEST_LIBC", library.data(), size) != size - 1) return finish(3, "cannot read ANYPS5_TEST_LIBC\n");
    library.resize(size - 1);
    for (int descriptor : {1, 2}) {
        if (::_fileno(__acrt_iob_func(descriptor)) != descriptor || ::_close(descriptor) != 0)
            return finish(4, "cannot close an inherited standard descriptor\n");
    }
    const auto previous = _set_thread_local_invalid_parameter_handler(InvalidParameter);
    const auto module = LoadLibraryW(library.c_str());
    const DWORD loadError = module == nullptr ? GetLastError() : ERROR_SUCCESS;
    _set_thread_local_invalid_parameter_handler(previous);
    if (module == nullptr) return finish(5, "cannot load libc: " + std::to_string(loadError) + "\n");
    if (invalidCalls != 0) return finish(6, "restoring bound standard streams invoked the invalid parameter handler " + std::to_string(invalidCalls) + " times\n");
    const auto put = reinterpret_cast<decltype(&fputc_nid_postfix)>(GetProcAddress(module, "fputc_nid_postfix"));
    const auto flush = reinterpret_cast<decltype(&fflush_nid_postfix)>(GetProcAddress(module, "fflush_nid_postfix"));
    if (!put || !flush) return finish(7, "cannot find guest stream operations\n");
    for (int descriptor : {1, 2}) {
        const auto pointer = GetProcAddress(module, descriptor == 1 ? "__stdoutp_nid_postfix" : "__stderrp_nid_postfix");
        if (!pointer) return finish(8, "cannot find a guest standard stream\n");
        auto* const guest = *reinterpret_cast<FileStream**>(pointer);
        auto* const host = __acrt_iob_func(descriptor);
        if (!guest || guest->GuestState().descriptor != descriptor || ::_fileno(host) != descriptor)
            return finish(9, "a restored stream has the wrong descriptor\n");
        if (std::fputc('x', host) != 'x' || std::fflush(host) != 0 || put('x', guest) != 'x' || flush(guest) != 0)
            return finish(10, "a restored stream cannot write to NUL\n");
    }
    return finish(0, "bound standard streams are restored without invalid parameter callbacks\n");
}
