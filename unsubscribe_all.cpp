#include "unsubscribe_all.hpp"

using namespace std::literals;
using namespace stompconn;

void unsubscribe_all::on_event(short ef)
{
    u::cout() << "disconnect: "sv << ef << std::endl;
    return;
    // любое событие приводик к закрытию сокета
    conn_.once(std::chrono::seconds(5), [&]{
        connect_localhost(std::chrono::seconds(20));
    });
}

void unsubscribe_all::on_connect()
{
    stomplay::logon logon("stompdemo"sv, "stompdemo"sv, "123"sv);
    logon.push(stomplay::header::heart_beat("10000,10000"sv));
    conn_.send(std::move(logon),
        std::bind(&unsubscribe_all::on_logon, this, std::placeholders::_1));
}

void unsubscribe_all::create_subscription()
{
    // очередь работает только на прием
    conn_.send(stomplay::subscribe("/queue/a3"sv, [&](auto msg){
        u::cout() << msg.dump() << std::endl;
    }), [&](auto subs) {
        u::cout() << subs.dump() << std::endl;
    });

    // очередь работает только на прием
    conn_.send(stomplay::subscribe("/queue/a4"sv, [&](auto msg){
        u::cout() << msg.dump() << std::endl;
    }), [&](auto subs) {
        u::cout() << subs.dump() << std::endl;
    });

    stomplay::subscribe a3("/queue/a5"sv, [&](auto msg) {
        u::cout() << msg.dump() << std::endl;
        // любое принятое сообщенеи приводит к отписке от этой очереди
        auto sub_id = msg.get_subscription();
        // отписываемся
        conn_.unsubscribe(sub_id, [&](auto unsubs){
            u::cout() << unsubs.dump() << std::endl;

            // отписка от очереди a3 приводит к отписке
            // от остальных очередей и отправки disconnect
            conn_.unsubscribe_logout([&]{
                u::cout() << "unsubscribe_logout"sv << std::endl;
            });
        });
    });

    // подписываемся
    conn_.send(std::move(a3), [&](stomplay::frame msg){
        u::cout() << msg.dump() << std::endl;
    });

}

void unsubscribe_all::on_logon(stomplay::frame logon)
{
    // проверяем была ли ошибка
    if (logon)
        create_subscription();
    else
    {
        conn_.disconnect([]{
            u::cout() << "disconnect"sv << std::endl;
        });
    }
}