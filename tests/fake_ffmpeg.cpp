#include <filesystem>
#include <fstream>
#include <windows.h>
// Test-only canary. Any invocation, even -version, records execution.
int main() {
    wchar_t path[32768]{};
    if (GetEnvironmentVariableW(L"LOGFORGE_FAKE_MARKER", path, 32768))
        std::ofstream(std::filesystem::path(path)) << "executed";
    return 17;
}
