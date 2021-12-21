#pragma once

#include <iostream>

namespace u {

std::ostream& output(std::ostream& os);

std::ostream& endl2(std::ostream& os);

std::ostream& cerr();

std::ostream& cout();

bool has_trace() noexcept;

void set_trace(bool value) noexcept;

template <class F>
static inline void trace(F fn)
{
    if (has_trace())
    {
        auto text = fn();
        auto size = static_cast<std::streamsize>(text.size());
        output(std::cout).write(text.data(), size);
    }
}

}