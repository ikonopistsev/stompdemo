#include "pingpong.hpp"

using namespace std::literals;

pingpong::pingpong(evdns_base* dns, event_base* queue,
    std::string read, std::string write)
    : dns_(dns)
    , queue_(queue)
    , read_(std::move(read))
    , write_(std::move(write))
{
    conn_.on_error([&](stompconn::packet p) {
        u::cout() << p.dump() << u::endl2;
    });
}

void pingpong::on_event(short ef)
{
    u::cout() << "disconnect: "sv << ef << std::endl;

    conn_.once(std::chrono::seconds(5), [&] {
         connect(address_, std::chrono::seconds(20));
    });
}

void pingpong::on_connect()
{
    stompconn::logon logon("stompdemo"sv, "stompdemo"sv, "123"sv);
    logon.push(stomptalk::header::heart_beat(10000, 10000));
    conn_.send(std::move(logon),
        std::bind(&pingpong::on_logon, this, std::placeholders::_1));
}

void pingpong::send_frame()
{
    auto msg_id = conn_.create_message_id();
    auto amqp_message_id = stomptalk::sv(msg_id);

    // write to a1
    stompconn::send frame(write_);
    // reply to a2
    frame.push(stomptalk::header::reply_to(read_));
    frame.push(stomptalk::header::time_since_epoch(stompconn::gettimeofday_cached(queue_)));
    frame.push(stomptalk::header::amqp_message_id(amqp_message_id));
    frame.push(stomptalk::header::ack_client_individual());

    // client send own session as data
    stompconn::buffer data;
    data.append(conn_.session());
    frame.payload(std::move(data));

    conn_.send(std::move(frame),[&](stompconn::packet send_receipt){
        if (!send_receipt)
        {
            u::cout() << send_receipt.dump() << u::endl2;
            on_event(BEV_EVENT_EOF);
        }
    });
}

void pingpong::on_logon(stompconn::packet logon)
{
    if (!logon)
    {
        u::trace([&] {
            return logon.dump();
        });

        // when you have going protocol parsing, 
        // you can't call disconnect() directly, 
        // you must use disconnect( fn )
        conn_.disconnect(connection::async());

        return;
    }

    // оформляем подписку

    stompconn::subscribe subs(read_, 
        std::bind(&pingpong::on_subscribe, this, std::placeholders::_1));

    conn_.send(std::move(subs), [this](stompconn::packet receipt) {
        if (!receipt) {
            u::trace([&] {
                return receipt.dump();
            });

            // when you have going protocol parsing, 
            // you can't call disconnect() directly, 
            // you must use disconnect( fn )
            conn_.disconnect(connection::async());

            return;
        }

        // a1 server, a2 clinet
        if (read_ == "a2"sv) 
        {
            // client
            send_frame();
        }
    });
}

void pingpong::on_subscribe(stompconn::packet frame)
{
    // a1 server, a2 client
    if (read_ == "a1"sv)
    {
        // server
        auto reply_to = frame.get_reply_to();
        if (!reply_to.empty())
        {
            auto msg_id = conn_.create_message_id();
            auto amqp_message_id = stomptalk::sv(msg_id);

            stompconn::send resp(reply_to);
            resp.push(stomptalk::header::time_since_epoch(stompconn::gettimeofday_cached(queue_)));
            resp.push(stomptalk::header::amqp_message_id(amqp_message_id));

            auto text = frame.payload().str();
            u::cout() << "server recv: "sv << text << std::endl;
            stompconn::buffer data;
            data.append(frame.payload());
            resp.payload(std::move(data));

            conn_.send(std::move(resp),[&](stompconn::packet receipt) {
                u::trace([&] {
                    return receipt.dump();
                });
            });
        }
    }
    else 
    {
        // client
        auto text = frame.payload().str();
        auto ses = conn_.session(); 
        u::cout() << "client recv: "sv << text << ((text == ses) ? " = "sv : " != "sv) << ses << std::endl;
    }

    auto ack = frame.get_ack();
    if (!ack.empty())
    {
        conn_.ack(frame, [](stompconn::packet receipt){
            u::trace([&] {
                return receipt.dump();
            });
        });
    }

}