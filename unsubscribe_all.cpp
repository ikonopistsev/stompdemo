#include "unsubscribe_all.hpp"

void unsubscribe_all::on_event(short ef)
{
    cout() << "disconnect: " << ef << std::endl;
    return;
    // любое событие приводик к закрытию сокета
    conn_.once(std::chrono::seconds(5), [&]{
        connect_localhost(std::chrono::seconds(20));
    });
}

void unsubscribe_all::on_connect()
{
    stompconn::logon logon("stompdemo", "stompdemo", "123");
    logon.push(stomptalk::header::heart_beat(10000, 10000));
    conn_.send(std::move(logon),
        std::bind(&unsubscribe_all::on_logon, this, std::placeholders::_1));
}

void unsubscribe_all::create_subscription()
{
    // очередь работает только на прием
    conn_.send(stompconn::subscribe("/queue/a1", [&](auto msg){
        cout() << msg.dump() << std::endl;
    }), [&](auto subs) {
        cout() << subs.dump() << endl2;
    });

    // очередь работает только на прием
    conn_.send(stompconn::subscribe("/queue/a2", [&](auto msg){
        cout() << msg.dump() << std::endl;
    }), [&](auto subs) {
        cout() << subs.dump() << endl2;
    });

    stompconn::subscribe a3("/queue/a3", [&](auto msg) {
        cout() << msg.dump() << std::endl;
        // любое принятое сообщенеи приводит к отписке от этой очереди
        auto sub_id = msg.get_subscription();
        // отписываемся
        conn_.unsubscribe(sub_id, [&](auto unsubs){
            cout() << unsubs.dump() << std::endl;

            // отписка от очереди a3 приводит к отписке
            // от остальных очередей и отправки disconnect
            conn_.unsubscribe_logout([&]{
                cout() << "unsubscribe_logout" << std::endl;
            });
        });
    });

    // подписываемся
    conn_.send(std::move(a3), [&](stompconn::packet msg){
        cout() << msg.dump() << endl2;
    });

}

void unsubscribe_all::on_logon(stompconn::packet logon)
{
    // проверяем была ли ошибка
    if (logon)
        create_subscription();
    else
    {
        conn_.disconnect([]{
            cout() << "disconnect" << std::endl;
        });
    }
}