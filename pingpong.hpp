#pragma once

#include "main.hpp"
#include "stompconn/connection.hpp"

class pingpong
{
    using connection = stompconn::connection;

    evdns_base* dns_{};
    event_base* queue_;
    std::string address_{};
    std::string read_{};
    std::string write_{};

    connection conn_{ queue_,
        std::bind(&pingpong::on_event, this, std::placeholders::_1),
        std::bind(&pingpong::on_connect, this)
    };

public:
    pingpong(evdns_base* dns, event_base* queue,
        std::string read, std::string write);

    template<class Rep, class Period>
    void connect(std::string address,
        std::chrono::duration<Rep, Period> timeout, int port = 61613)
    {
        using namespace std::literals;
        
        address_ = std::move(address);

        u::cout() << marker() << " connect to: "sv << address_ << std::endl;
        
        conn_.connect(dns_, address_, port, timeout);
    }

    std::string_view marker() const noexcept;

    void on_event(short ef);

    void on_connect();

    void send_frame();

    void on_logon(stompconn::packet logon);

    void on_subscribe(stompconn::packet frame);
};
