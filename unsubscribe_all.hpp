#pragma once

#include "main.hpp"
#include "stompconn/connection.hpp"

// тест подключения подписки и отписки
class unsubscribe_all
{
    using connection = stompconn::connection;
    event_base* queue_{};

    connection conn_{ queue_,
        std::bind(&unsubscribe_all::on_event, this, std::placeholders::_1),
        std::bind(&unsubscribe_all::on_connect, this)
    };

public:
    explicit unsubscribe_all(event_base* queue)
        : queue_(queue)
    {   }

    template<class Rep, class Period>
    void connect(const std::string& host, int port, std::chrono::duration<Rep, Period> timeout)
    {
        conn_.connect(nullptr, host, port, timeout);
    }

    template<class Rep, class Period>
    void connect_localhost(std::chrono::duration<Rep, Period> timeout, int port = 61613)
    {
        conn_.connect(nullptr, "127.0.0.1", 61613, timeout);
    }

    void on_event(short ef);

    void on_connect();

    void create_subscription();

    void on_logon(stompconn::packet logon);
};