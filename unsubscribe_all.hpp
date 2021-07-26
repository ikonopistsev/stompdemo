#pragma once

#include "main.hpp"
#include "stompconn/connection.hpp"
#include "btpro/queue.hpp"

// тест подключения подписки и отписки
class unsubscribe_all
{
    using connection = stompconn::connection;
    btpro::queue& queue_;

    connection conn_{ queue_,
        std::bind(&unsubscribe_all::on_event, this, std::placeholders::_1),
        std::bind(&unsubscribe_all::on_connect, this)
    };

public:
    explicit unsubscribe_all(btpro::queue& queue)
        : queue_(queue)
    {   }

    template<class Rep, class Period>
    void connect_localhost(std::chrono::duration<Rep, Period> timeout, int port = 61613)
    {
        conn_.connect(btpro::ipv4::loopback(port), timeout);
    }

    void on_event(short ef);

    void on_connect();

    void create_subscription();

    void on_logon(stompconn::packet logon);
};