#pragma once

#include <iostream>

namespace u {

std::ostream& output(std::ostream& os);

std::ostream& cerr();

std::ostream& cout();

static bool allow_trace = false;

template <class F>
static inline void trace(F fn)
{
    if (allow_trace)
    {
        auto text = fn();
        auto size = static_cast<std::streamsize>(text.size());
        std::endl(output(std::cout).write(text.data(), size));
    }
}

}