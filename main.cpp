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

std::ostream& cerr()
{
    return output(std::cerr);
}

std::ostream& cout()
{
    return output(std::cout);
}

}

using namespace std::literals;

namespace {

auto create_queue()
{
    u::cout() << "stompconn: v"sv << stompconn::version() << std::endl;
    u::cout() << "stomptalk: v"sv << stomptalk_version() << std::endl;
    u::cout() << "libevent-"sv << event_get_version() << std::endl;

    auto remove = [](event_base* ptr){
        event_base_free(ptr);
    };
    return std::unique_ptr<event_base, 
        decltype(remove)>{event_base_new(), std::move(remove)};
}

auto create_dns(event_base* queue)
{
    assert(queue);
    auto dns = evdns_base_new(queue, EVDNS_BASE_INITIALIZE_NAMESERVERS);
    auto remove = [](evdns_base* ptr){
        evdns_base_free(ptr, DNS_ERR_SHUTDOWN);
    };
    return std::unique_ptr<evdns_base, 
        decltype(remove)>{dns, remove};
}

} 

int main(int argc, char *argv[])
{
    std::string host{"threadtux.lan"sv};
    //std::string host{"localhost"sv};
    //std::string host{"u1.lan"sv};
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

        auto q = create_queue();
        auto queue = q.get();

        evdns_base* dns = nullptr;
        auto d = create_dns(queue);
        dns = d.get();

        auto server_queue = std::string{"/queue/server-rpc"sv};
        pingpong server(dns, queue, server_queue, {});
        pingpong client(dns, queue, {}, server_queue);

        server.connect(host, std::chrono::seconds(20));
        client.connect(host, std::chrono::seconds(20));

        //unsubscribe_all unsubs(queue);
        //unsubs.connect_localhost(std::chrono::seconds(20));

        event_base_dispatch(queue);
    }
    catch (const std::exception& e)
    {
        u::cerr() << e.what() << std::endl;
    }

    return 0;
}

