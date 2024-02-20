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
#include <atomic>

#ifndef _WIN32
#include <signal.h>
#endif // _WIN32

using namespace std::literals;

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

} // namespace u

namespace {

struct event_erase {
    void operator()(event_base* ptr) const noexcept {
        event_base_free(ptr);
    }
    void operator()(evdns_base* ptr) const noexcept {
        evdns_base_free(ptr, DNS_ERR_SHUTDOWN);
    }
};

auto create_queue()
{
    u::cout() << "stompconn: v"sv << stompconn::version() << std::endl;
    u::cout() << "stomptalk: v"sv << stomptalk_version() << std::endl;
    u::cout() << "libevent-"sv << event_get_version() << std::endl;
    auto base = event_base_new();
    return std::unique_ptr<event_base, event_erase>{base};
}

auto create_dns(event_base* queue)
{
    assert(queue);
    auto dns = evdns_base_new(queue, EVDNS_BASE_INITIALIZE_NAMESERVERS);
    return std::unique_ptr<evdns_base, event_erase>{dns};
}

class justsubscribe
{
    using connection = stompconn::connection;

    evdns_base* dns_{};
    event_base* queue_{};

    std::size_t try_subs_{ 16 };

    connection conn_{ queue_, 
        [&](auto ef) { on_event(ef); } 
    };

public:
    justsubscribe(evdns_base* dns, event_base* queue)
        : dns_{dns}
        , queue_{queue}
    {   
        // прием сообщения об ошибке
        // от сервера: кадр ERROR
        // https://stomp.github.io/stomp-specification-1.2.html#ERROR
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
            {
                u::cerr() << "connection closed"sv << std::endl;
            }

            if (ef & BEV_EVENT_ERROR)
                u::cerr() << "connection error"sv << std::endl;

            if (ef == (BEV_EVENT_READING|BEV_EVENT_TIMEOUT))
                u::cerr() << "connection timeout"sv << std::endl;

            // для примера остановим обработку очереди
            event_base_loopbreak(queue_);
       }
    }

    void do_subscribe()
    {
        stompconn::stomplay::subscribe subs("/queue/just_subs"sv, [&](auto frame) {
            u::cout() << "RECV: " << frame.dump() << std::endl;            
        });


        // отправим команду на подписку
        conn_.send(std::move(subs), [&](auto frame) {
            u::cout() << "SUBS OK: "sv << frame.dump() << std::endl;

            // подписываемся и отписываемся несколько раз
            if (--try_subs_ > 0)
            {
                conn_.once(std::chrono::milliseconds(0), 
                    [&, subs_id = std::string{frame.get_subscription()}] {
                        do_unsubscribe(subs_id);
                });
            }
            else
            {
                u::cout() << "done"sv << std::endl;
            }
        });
    }

    void do_unsubscribe(std::string_view id)
    {
        conn_.unsubscribe(id, [&](auto frame) {
            u::cout() << "UNSUBS OK: "sv << frame.dump() << std::endl;

            conn_.once(std::chrono::milliseconds(0), 
                [&] {
                    do_subscribe();
            });
        });
    }

    void on_connect()
    {
        u::cout() << "tcp ok"sv << std::endl;

        stompconn::stomplay::logon cmd("/"sv, "stomp"sv, "st321"sv);
        cmd.push(stompconn::stomplay::header::heart_beat(30000, 30000));
        u::cout() << cmd.dump() << std::endl;

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

            do_subscribe();

            // отключимся через 10 секунд
            // после успешной авторизации
            // logout(std::chrono::seconds(60));
        });
    }

    template<class T>
    void logout(T timeout)
    {
        conn_.once(timeout, [&]{
            try {
                // отправим команду на отключение
                // сервер разорвет соединение после ответа
                conn_.logout([](auto frame) {
                    u::cout() << frame.dump() << std::endl;
                });
            }
            catch (const std::exception& e)
            {
                u::cerr() << e.what() << std::endl;
            }
        });
    }
};

}

int main(int argc, char *argv[])
{
    // in /etc/hosts
    std::string host{"rabbitmq.lan"sv};
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
        auto d = create_dns(queue);

        u::cout() << "just connect!"sv << std::endl;

        justsubscribe c{d.get(), queue};
        
        // подключаемся к серверу
        c.connect(host, std::chrono::seconds(20));

        event_base_dispatch(queue);
    }
    catch (const std::exception& e)
    {
        u::cerr() << e.what() << std::endl;
    }

    return 0;
}
