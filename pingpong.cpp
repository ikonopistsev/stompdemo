#include "pingpong.hpp"

using namespace std::literals;
using namespace stompconn;

pingpong::pingpong(evdns_base* dns, event_base* queue,
    std::string read, std::string write)
    : dns_{dns}
    , queue_{queue}
    , server_{!read.empty()}
    , read_{std::move(read)}
    , write_{std::move(write)}
{
    conn_.on_error([&](stomplay::frame p) {
        u::cout() << marker() << p.dump() << std::endl;
    });
}

void pingpong::on_event(short ef)
{
    u::cout() << marker() << "disconnect: "sv << ef << std::endl;

    conn_.once(std::chrono::seconds(5), [&] {
         connect(address_, std::chrono::seconds(20));
    });
}

void pingpong::on_connect()
{
    u::cout() << marker() << "connected"sv << std::endl;

    stomplay::logon logon("stompdemo"sv, "stompdemo"sv, "123"sv);
    logon.push(stomplay::header::heart_beat(10000, 10000));
    conn_.send(std::move(logon),
        std::bind(&pingpong::on_logon, this, std::placeholders::_1));
}

// send frame to server and subscribe on temp queue
void pingpong::send_with_subscribe()
{
    std::string reply_to{"/temp-queue/"sv};
    reply_to += conn_.session();

    // this do autosubscribe 
    stompconn::send_temp frame{write_, reply_to,
        std::bind(&pingpong::on_reply, this, std::placeholders::_1)};

    frame.push(stompconn::header::timestamp(stompconn::gettimeofday_cached(queue_)));
    frame.push(stompconn::header::amqp_message_id(conn_.create_message_id()));
    frame.push(stompconn::header::ack_client_individual());

    // client send own session as data
    stompconn::buffer data;
    data.append(conn_.session());
    frame.payload(std::move(data));

    u::cout() << marker() << "send to "sv << write_ << std::endl;

    conn_.send(std::move(frame),[&, reply_to](stompconn::packet receipt){
        if (!receipt) {
            u::cout() << receipt.dump() << std::endl;
            on_event(BEV_EVENT_EOF);
        } else {
            u::trace([&] {
                return receipt.dump();
            });

            u::cout() << marker()<< "subscribe on "sv << reply_to << std::endl;
        }
    });
}

// send frame to server
void pingpong::send_frame()
{
    // this do autosubscribe 
    stompconn::send frame{write_};

    std::string reply_to{"/temp-queue/"sv};
    reply_to += conn_.session();

    frame.push(stompconn::header::reply_to(reply_to));
    frame.push(stompconn::header::timestamp(stompconn::gettimeofday_cached(queue_)));
    frame.push(stompconn::header::amqp_message_id(conn_.create_message_id()));
    frame.push(stompconn::header::ack_client_individual());

    // client send own session as data
    stompconn::buffer data;
    data.append(conn_.session());
    frame.payload(std::move(data));

    conn_.send(std::move(frame),[&](stompconn::packet receipt){
        if (!receipt) {
            u::cout() << receipt.dump() << std::endl;
            on_event(BEV_EVENT_EOF);
        } else {
            u::trace([&] {
                return receipt.dump();
            });
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
    u::cout() << marker() << "logon"sv << std::endl;    
    if (server_)
    {
        stompconn::subscribe subs(read_, 
            std::bind(&pingpong::on_subscribe, this, std::placeholders::_1));
        subs.push(stompconn::header::prefetch_count(1));

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

            u::cout() << marker()<< "subscribe on "sv << read_ << std::endl;
        });
    }
    else
    {
        // client
        send_with_subscribe();
    }
}

void pingpong::on_subscribe(stompconn::packet frame)
{
    // server
    auto reply_to = frame.get_reply_to();
    if (!reply_to.empty())
    {
        auto msg_id = conn_.create_message_id();

        stompconn::send resp(reply_to);
        resp.push(stompconn::header::timestamp(stompconn::gettimeofday_cached(queue_)));
        resp.push(stompconn::header::amqp_message_id(msg_id));

        auto text = frame.payload().str();
        //u::cout() << "server recv: "sv << text << std::endl;
        stompconn::buffer data;
        data.append(frame.payload());
        resp.payload(std::move(data));

        conn_.send(std::move(resp),[&](stompconn::packet receipt) {
            u::trace([&] {
                return receipt.dump();
            });
        });
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

// server reply
void pingpong::on_reply(stompconn::packet frame)
{
    // прием ответов от сервера
    auto text = frame.payload().str();
    auto ses = conn_.session();

    if (text != ses)
        u::cout() << "client recv: "sv << text << ((text == ses) ? " = "sv : " != "sv) << ses << std::endl;

    send_frame();

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