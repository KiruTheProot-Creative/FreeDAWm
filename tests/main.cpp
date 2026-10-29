#include <cstdio>
#include <cstring>

#include "TestHarness.h"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failed = 0;
    for (auto& t : testRegistry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        const int before = failureCount();
        t.fn();
        const bool ok = failureCount() == before;
        std::printf("[%s] %s\n", ok ? " ok " : "FAIL", t.name);
        ++ran;
        if (!ok) ++failed;
    }
    std::printf("\n%d tests, %d failed\n", ran, failed);
    return failed == 0 ? 0 : 1;
}
