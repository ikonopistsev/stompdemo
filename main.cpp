#include "main.hpp"
#include "pingpong.hpp"
#include "unsubscribe_all.hpp"
#include "stompconn/connection.hpp"
#include "stompconn/version.hpp"
#include "stomptalk/parser.h"

#include <list>
#include <string_view>
#include <functional>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <stdlib.h>

#ifndef _WIN32
#include <signal.h>
#endif // _WIN32

namespace u {

std::ostream& output(std::ostream& os)
{
    namespace ch = std::chrono;
    auto n = ch::system_clock::now();
    auto d = n.time_since_epoch();
    auto ms = ch::duration_cast<ch::milliseconds>(d).count() % 1000;
    auto t = static_cast<std::time_t>(ch::duration_cast<ch::seconds>(d).count());
    auto tm = *std::gmtime(&t);
    return os << std::put_time(&tm, "%FT%T") 
        << '.' << std::setfill('0') << std::setw(3) << ms << 'Z' << ' ';
}

std::ostream& endl2(std::ostream& os)
{
    return os << std::endl << std::endl;
}

std::ostream& cerr()
{
    return output(std::cerr);
}

std::ostream& cout()
{
    return output(std::cout);
}

static bool trace_output = false;

bool has_trace() noexcept
{
    return trace_output;
}

void set_trace(bool value) noexcept
{
    trace_output = value;
}

}

using namespace std::literals;

namespace {

event_base* create_queue()
{
    u::cout() << "stompconn: v"sv << stompconn::version() << std::endl;
    u::cout() << "stomptalk: v"sv << stomptalk_version() << std::endl;
    u::cout() << "libevent-"sv << event_get_version() << std::endl;
    auto queue = event_base_new();
    assert(queue);
    return queue;
}

} 

int main(int argc, char *argv[])
{
    std::string host = "127.0.0.1";
    //small_test();
    if (argc > 1)
        host = argv[1];

    try
    {
#ifdef _WIN32
        {
            WSADATA w;
            ::WSAStartup(MAKEWORD(2, 2), &w);
        }
#endif // _WIN32

        //u::set_trace(true);
        auto queue = create_queue();
        evdns_base* dns = nullptr;
        // dns = evdns_base_new(queue, EVDNS_BASE_INITIALIZE_NAMESERVERS);

        pingpong server(dns, queue, "a1", "a2");
        pingpong client(dns, queue, "a2", "a1");

        server.connect(host, std::chrono::seconds(20));
        client.connect(host, std::chrono::seconds(20));

        unsubscribe_all unsubs(queue);
        unsubs.connect_localhost(std::chrono::seconds(20));

        event_base_dispatch(queue);

        // with dns it never exit normly
        // it just example
        if (dns)
            evdns_base_free(dns, DNS_ERR_SHUTDOWN);
            
        event_base_free(queue);
    }
    catch (const std::exception& e)
    {
        u::cerr() << e.what() << std::endl;
    }

    return 0;
}

