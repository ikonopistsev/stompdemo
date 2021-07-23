#pragma once

#include <iostream>

std::ostream& output(std::ostream& os);

std::ostream& endl2(std::ostream& os);

std::ostream& cerr();

std::ostream& cout();

template <class F>
std::ostream& cout(F fn)
{
    auto text = fn();
    return std::endl(output(std::cout)
        .write(text.data(), static_cast<std::streamsize>(text.size())));
}

template <class F>
std::ostream& cerr(F fn)
{
    auto text = fn();
    return std::endl(output(std::cerr)
        .write(text.data(), static_cast<std::streamsize>(text.size())));
}