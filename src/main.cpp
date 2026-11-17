#include "app.h"

#include <cstdio>
#include <exception>

int main()
{
    try {
        App app(800, 600);
        app.run();
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }

    return 0;
}
