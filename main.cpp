#include "main.hpp"
#include "pingpong.hpp"
#include "unsubscribe_all.hpp"
#include "stompconn/connection.hpp"
#include "stompconn/version.hpp"
#include "stomptalk/version.hpp"

#include <list>
#include <string_view>
#include <functional>
#include <stdlib.h>

#ifndef _WIN32
#include <signal.h>
#endif // _WIN32

namespace u {

std::ostream& output(std::ostream& os)
{
    time_t t = time(nullptr); 
    auto ts = std::string_view(ctime(&t));
    os << ts.substr(0, ts.size() - 1) << ' ';
    return os;
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
    u::cout() << "stomptalk: v"sv << stomptalk::version() << std::endl;
    u::cout() << "libevent-"sv << event_get_version() << std::endl;

    auto queue = event_base_new();
    assert(queue);
    return queue;
}

} 

int main()
{
    try
    {
#ifdef _WIN32
        {
            WSADATA w;
            ::WSAStartup(MAKEWORD(2, 2), &w);
        }
#endif // _WIN32

        auto queue = create_queue();
        evdns_base* dns = nullptr;
        dns = evdns_base_new(queue, EVDNS_BASE_INITIALIZE_NAMESERVERS);

        pingpong server(dns, queue, "a1", "a2");
        pingpong client(dns, queue, "a2", "a1");

        server.connect("127.0.0.1", std::chrono::seconds(20));
        client.connect("127.0.0.1", std::chrono::seconds(20));

        //unsubscribe_all unsubs(queue);
        //unsubs.connect_localhost(std::chrono::seconds(20));

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

