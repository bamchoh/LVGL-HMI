#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    // Windows sleeps in steps of 15.6 ms by default: every sleep_ms(5) of a driver or of wait_for()
    // took 15.6 ms, and the tests that talk to a fake PLC hundreds of times took most of the run.
    // 1 ms steps for the tests only (the runtime keeps the default).
    timeBeginPeriod(1);
#endif
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    const int result = context.run();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
    return result;
}
