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

    struct erase_event_base {
        void operator()(event_base* ptr) const {
            event_base_free(ptr);
        }
    };
    return std::unique_ptr<event_base, erase_event_base>{event_base_new()};
}

auto create_dns(event_base* queue)
{
    assert(queue);
    auto dns = evdns_base_new(queue, EVDNS_BASE_INITIALIZE_NAMESERVERS);
    auto remove = [](evdns_base* ptr){
        evdns_base_free(ptr, DNS_ERR_SHUTDOWN);
    };
    return std::unique_ptr<evdns_base, 
        decltype(remove)>{dns, std::move(remove)};
}

class justconnect
{
    using connection = stompconn::connection;

    evdns_base* dns_{};
    event_base* queue_{};

    connection conn_{ queue_, 
        [&](auto ef) { on_event(ef); } };

public:
    justconnect(evdns_base* dns, event_base* queue)
        : dns_{dns}
        , queue_{queue}
    {   
        // прием сообщения об ошибке
        conn_.on_error([](auto frame) {
            u::cerr() << frame.dump() << std::endl;
        });
    }


    template<class Rep, class Period>
    void connect(const std::string& host,
        std::chrono::duration<Rep, Period> timeout, int port = 61613)
    {
        using namespace std::literals;
        u::cout() << "connect to: "sv << host << std::endl;
        conn_.connect(dns_, host, port, timeout);
    }

    void on_event(short ef)
    {
        if (ef == BEV_EVENT_CONNECTED)
            on_connect();
        else
        {
            if (ef == (BEV_EVENT_WRITING|BEV_EVENT_TIMEOUT))
                u::cerr() << "unable to connect"sv << std::endl;
            
            if (ef & BEV_EVENT_EOF)
                u::cerr() << "connection closed"sv << std::endl;

            if (ef & BEV_EVENT_ERROR)
                u::cerr() << "connection error"sv << std::endl;

            if (ef == (BEV_EVENT_READING|BEV_EVENT_TIMEOUT))
                u::cerr() << "connection timeout"sv << std::endl;
        }
    }

    void on_connect()
    {
        u::cout() << "tcp ok"sv << std::endl;

        stompconn::stomplay::logon cmd("/"sv, "stomp"sv, "st321"sv);
        cmd.push(stompconn::stomplay::header::heart_beat(3000, 3000));

        auto cmd_str = cmd.str();
        // заменим протокольные переносы строк на символ ';' кроме первого
        std::replace(std::begin(cmd_str), std::begin(cmd_str) + 8, '\n', ' ');
        std::replace(std::begin(cmd_str) + 8, std::end(cmd_str), '\n', ';');
        u::cout() << cmd_str << std::endl;

        // после отправки cmd приходит в негодность
        conn_.send(std::move(cmd), [&](auto frame) {
            // полный дамп фремйма
            u::cout() << frame.dump() << std::endl;
            // выводим интересущие нас заголовки по имени
            u::cout() << "+ 1.server: "sv << frame.get("server"sv) << std::endl;
            // и по идентификатору
            u::cout() << "+ 2.server: "sv << frame.get(st_header_server) << std::endl;
            u::cout() << "+ 1.session: "sv << frame.get(st_header_session) << std::endl;
            u::cout() << "+ 2.session: "sv << frame.session() << std::endl;
        });
    }

    template<class Rep, class Period>
    void logout(std::chrono::duration<Rep, Period> timeout)
    {
        conn_.at_running([&]{
            conn_.once(timeout, [&]{
                conn_.logout([](auto frame) {
                    u::cout() << frame.dump() << std::endl;
                });
            });
        });
    }
};

} 

int main(int argc, char *argv[])
{
    std::string host{"server.lan"sv};
    //std::string host{"localhost"sv};
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
        //auto d = create_dns(queue);

        justconnect c{nullptr, queue};
        
        // подключаемся к серверу
        c.connect(host, std::chrono::seconds(20));

        // отключаемся через
        c.logout(std::chrono::seconds(20));

        event_base_dispatch(queue);
    }
    catch (const std::exception& e)
    {
        u::cerr() << e.what() << std::endl;
    }

    return 0;
}
