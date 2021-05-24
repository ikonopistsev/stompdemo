#include "btpro/evcore.hpp"
#include "btpro/evstack.hpp"
#include "btpro/uri.hpp"
#include "btdef/date.hpp"
#include "btpro/tcp/bevfn.hpp"
#include "btpro/sock_addr.hpp"
#include "stomptalk/parser.hpp"
#include "stomptalk/parser_hook.hpp"
#include "stomptalk/antoull.hpp"
#include "stompconn/connection.hpp"
#include "stompconn/version.hpp"
#include "stomptalk/version.hpp"

#include <iostream>
#include <list>
#include <string_view>
#include <functional>

#ifndef _WIN32
#include <signal.h>
#endif // _WIN32

using namespace std::literals;

std::ostream& output(std::ostream& os)
{
    auto log_time = btdef::date::log_time_text();
    os << log_time << ' ';
    return os;
}

inline std::ostream& endl2(std::ostream& os)
{
    return os << std::endl << std::endl;
}

inline std::ostream& cerr()
{
    return output(std::cerr);
}

inline std::ostream& cout()
{
    return output(std::cout);
}

template <class F>
std::ostream& cout(F fn)
{
    auto text = fn();
    return std::endl(output(std::cout)
        .write(text.data(), static_cast<std::streamsize>(text.size())));
}

template <class F>
std::ostream& cerr(F fn)
{
    auto text = fn();
    return std::endl(output(std::cerr)
        .write(text.data(), static_cast<std::streamsize>(text.size())));
}

btpro::queue create_queue()
{
    btpro::config conf;

    cout() << "stompconn: v"sv << stompconn::version() << std::endl;
    cout() << "stomptalk: v"sv << stomptalk::version() << std::endl;
    cout([&]{
        std::string text(64, '\0');
        text = "libevent-"sv;
        text += btpro::queue::version();
        text += " -"sv;
        for (auto& i : conf.supported_methods())
        {
            if (!text.empty())
                text += ' ';
            text += i;
        }
        return  text;
    });

#ifndef _WIN32
    //conf.require_features(EV_FEATURE_ET|EV_FEATURE_O1);
#endif //
    btpro::queue q;

    q.create(conf);
    return q;
}

class peer1
{
    using connection = stompconn::connection;

    btpro::queue_ref queue_;
    btpro::dns_ref dns_;
    std::string host_{};
    int port_{};
    std::size_t trasaction_id_{};

    connection conn_{ queue_,
        std::bind(&peer1::on_event, this, std::placeholders::_1),
        std::bind(&peer1::on_connect, this)
    };

public:
    peer1(btpro::queue_ref queue, btpro::dns_ref dns)
        : queue_(queue)
        , dns_(dns)
    {   }

    template<class Rep, class Period>
    void connect(const std::string& host, int port,
                 std::chrono::duration<Rep, Period> timeout)
    {
        cout([&]{
            std::string text;
            text += "connect to: "sv;
            text += host;
            if (port)
                text += ' ' + std::to_string(port);
            return text;
        });

        host_ = host;
        port_ = port;

        conn_.connect(dns_, host, port, timeout);
    }

    void on_event(short)
    {
        // любое событие приводик к закрытию сокета
        queue_.once(std::chrono::seconds(5), [&](...){
            connect(host_, port_, std::chrono::seconds(20));
        });
    }

    void on_connect()
    {
        stompconn::logon logon("two"sv, "max"sv, "maxtwo"sv);
        //logon.push(stomptalk::header::receipt("123"));
        conn_.send(std::move(logon),
            std::bind(&peer1::on_logon, this, std::placeholders::_1));
    }

    void disconnect(const stompconn::packet& packet)
    {
        cerr() << packet.dump() << std::endl;
        on_event(BEV_EVENT_EOF);
    }

    void on_logon(stompconn::packet logon)
    {
        if (!logon) {
            disconnect(logon); return;
        }

        auto commit_transaction = [&](...){
            stompconn::commit frame(std::to_string(trasaction_id_));
            frame.push(stomptalk::header::time_since_epoch());
            conn_.send(std::move(frame),
                std::bind(&peer1::on_logon, this, std::placeholders::_1));
        };

        auto second_send = [&, commit_transaction](...){
            stompconn::send frame("/queue/transaction_demo");
            frame.push(stomptalk::header::time_since_epoch());
            frame.push(stomptalk::header::transaction(trasaction_id_));
            frame.push(stomptalk::header::content_type_text_plain());
            frame.payload(btpro::buffer(conn_.create_message_id()));
            conn_.send(std::move(frame));
            queue_.once(std::chrono::seconds(30), commit_transaction);
        };

        auto first_send = [&, second_send](...){
            stompconn::send frame("/queue/transaction_demo");
            frame.push(stomptalk::header::time_since_epoch());
            frame.push(stomptalk::header::transaction(trasaction_id_));
            frame.push(stomptalk::header::content_type_text_plain());
            frame.payload(btpro::buffer(conn_.create_message_id()));
            conn_.send(std::move(frame));
            queue_.once(std::chrono::seconds(2), second_send);
        };

        auto begin_receipt = [=](auto packet) {
            if (!packet) {
                disconnect(packet); return;
            }
            queue_.once(std::chrono::seconds(2), first_send);
        };

        auto begin_transaction = [&, begin_receipt](...){
            stompconn::begin frame(++trasaction_id_);
            frame.push(stomptalk::header::time_since_epoch());
            frame.push(stomptalk::header::expires(std::chrono::seconds(20)));
            conn_.send(std::move(frame), begin_receipt);
        };

        queue_.once(std::chrono::seconds(2), begin_transaction);
    }
};

// тест подключения подписки и отписки
class peer2
{
    using connection = stompconn::connection;
    btpro::queue_ref queue_;

    connection conn_{ queue_,
        std::bind(&peer2::on_event, this, std::placeholders::_1),
        std::bind(&peer2::on_connect, this)
    };

public:
    explicit peer2(btpro::queue_ref queue)
        : queue_(queue)
    {   }

    template<class Rep, class Period>
    void connect_localhost(std::chrono::duration<Rep, Period> timeout)
    {
        conn_.connect(btpro::ipv4::loopback(61613), timeout);
    }

    void on_event(short ef)
    {
        cout() << "disconnect: " << ef << std::endl;
        // любое событие приводик к закрытию сокета
        queue_.once(std::chrono::seconds(5), [&](...){
            connect_localhost(std::chrono::seconds(20));
        });
    }

    void on_connect()
    {
        stompconn::logon logon("two", "max", "maxtwo");
        conn_.send(std::move(logon),
            std::bind(&peer2::on_logon, this, std::placeholders::_1));
    }

    void on_logon(stompconn::packet logon)
    {
        // проверяем была ли ошибка
        if (logon)
        {
            // если ошибки не было
            // формируем запрос на подписку
            stompconn::subscribe subs("/queue/mt4_trades",
                                      [&](stompconn::packet msg) {
                // просто выдаем пришедшие по подписке данные
                cout() << msg.dump() << std::endl;

                if (msg)
                {
                    // получаем идентификатор подписки
                    auto sub_id = msg.get_subscription();

                    // отписываемся
                    conn_.unsubscribe(sub_id, [&](stompconn::packet unsubs){
                        // просто выдаем пришедшие по подписке данные
                        cout() << unsubs.dump() << std::endl;

                        if (!unsubs)
                            disconnect();
                    });
                }
                else
                    disconnect();
            });

            // подписываемся
            conn_.send(std::move(subs), [&](stompconn::packet subs){
                // дамп пакета
                cout() << subs.dump() << endl2;

                if (!subs)
                    disconnect();
            });
        }
        else // была ошика - отключаемся
            disconnect();
    }

    void disconnect()
    {
        on_event(BEV_EVENT_EOF);
    }
};

std::size_t rpc_client_num = {};
// rpc
class rpc
{
    using connection = stompconn::connection;

    btpro::queue_ref queue_;
    btpro::dns_ref dns_;
    std::string host_{};
    int port_{};

    std::string read_{};
    std::string write_{};

    std::size_t msg_count_{};

    connection conn_{ queue_,
        std::bind(&rpc::on_event, this, std::placeholders::_1),
        std::bind(&rpc::on_connect, this)
    };

public:
    rpc(btpro::queue_ref queue, btpro::dns_ref dns,
        std::string read, std::string write)
        : queue_(queue)
        , dns_(dns)
        , read_(read)
        , write_(write)
    {
        conn_.on_error([&](stompconn::packet p) {
            //cout() << p.get(stomptalk::header::message()) << std::endl;
            //cout() << p.payload().str() << std::endl;
            cout() << p.dump() << endl2;
        });
    }

    template<class Rep, class Period>
    void connect(const std::string& host, int port,
                 std::chrono::duration<Rep, Period> timeout)
    {
        cout([&]{
            std::string text;
            text += "connect to: "sv;
            text += host;
            if (port)
                text += ' ' + std::to_string(port);
            return text;
        });

        host_ = host;
        port_ = port;

        conn_.connect(dns_, host, port, timeout);
    }

    void on_event(short ef)
    {
        cout() << "disconnect: " << ef << std::endl;
        // любое событие приводик к закрытию сокета
        queue_.once(std::chrono::seconds(5), [&](...){
            connect(host_, port_, std::chrono::seconds(20));
        });
    }

    void on_connect()
    {
            // artemis
//        conn_.send(stompconn::logon("/", "admin", "123"),
//            std::bind(&rpc::on_logon, this, std::placeholders::_1));
        stompconn::logon logon("stompdemo", "stompdemo", "123");
        //logon.push(stomptalk::header::heart_beat(1000, 1000));
        conn_.send(std::move(logon),
            std::bind(&rpc::on_logon, this, std::placeholders::_1));
    }

    void send_frame()
    {
        auto msg_id = conn_.create_message_id();
        auto amqp_message_id = stomptalk::sv(msg_id);

        stompconn::send frame(write_);
        frame.push(stomptalk::header::reply_to(read_));
        frame.push(stomptalk::header::time_since_epoch(queue_.gettimeofday_cached()));
        frame.push(stomptalk::header::amqp_message_id(amqp_message_id));

        std::string text = conn_.session();
        text += '-';
        text += amqp_message_id;
        btpro::buffer data(text);
//                        std::string text;
//                        text.resize(1024, 'x');
//                        text.resize(1024*2, 'x');
//                        text.resize(1024*4, 'x');
//                       text.resize(1024*8, 'x');
//                        text.resize(1024*14, 'x');
//                        text.resize(1024*16, 'x');
//                        data.append(text);
        frame.payload(std::move(data));

        cout() << "SEND "sv << text << std::endl;

        conn_.send(std::move(frame),[&](stompconn::packet send_receipt){
            if (!send_receipt)
            {
                cout() << send_receipt.dump() << endl2;
                on_event(BEV_EVENT_EOF);
            }
        });
    }

    void on_logon(stompconn::packet logon)
    {
        if (logon)
        {
            // формируем подписку
            stompconn::subscribe subs(read_, [this](stompconn::packet msg){
                if (msg)
                {
                    if (++msg_count_ < 1000000)
                    {
                        auto reply = msg.get_reply_to();
                        if (!reply.empty())
                        {
                            auto msg_id = conn_.create_message_id();
                            auto amqp_message_id = stomptalk::sv(msg_id);

                            stompconn::send frame(reply);
                            frame.push(stomptalk::header::reply_to(read_));
                            frame.push(stomptalk::header::time_since_epoch(
                                queue_.gettimeofday_cached()));
                            frame.push(stomptalk::header::amqp_message_id(amqp_message_id));

                            btpro::buffer buf;
                            msg.copyout(buf);
                            frame.payload(std::move(buf));

                            conn_.send(std::move(frame),[&](stompconn::packet send_receipt){
                                if (!send_receipt)
                                {
                                    cout() << send_receipt.dump() << endl2;
                                    on_event(BEV_EVENT_EOF);
                                }
                            });
                        }
                    }
                    else
                    {
#ifdef NDEBUG
                        queue_.loop_break();
#endif
                    }
                }
                else
                {
                    cout() << msg.dump() << endl2;
                    on_event(BEV_EVENT_EOF);
                }
            });

            // оформляем подписку
            conn_.send(std::move(subs), [this](stompconn::packet receipt){
                if (receipt)
                {
                    // если мы первые формируем первое сообщение
                    if (read_ == "a1")
                    {
                        for (std::size_t i = 0; i < 5; ++i)
                            send_frame();
                    }
                }
                else
                {
                    cout() << receipt.dump() << endl2;
                    on_event(BEV_EVENT_EOF);
                }
            });
        }
        else
        {
            cout() << logon.dump() << endl2;
            on_event(BEV_EVENT_EOF);
        }
    }
};

class peer4
{
    using connection = stompconn::connection;

    btpro::queue_ref queue_;
    btpro::dns_ref dns_;
    std::string host_{};
    int port_{};

    connection conn_{ queue_,
        std::bind(&peer4::on_event, this, std::placeholders::_1),
        std::bind(&peer4::on_connect, this)
    };

public:
    peer4(btpro::queue_ref queue, btpro::dns_ref dns)
        : queue_(queue)
        , dns_(dns)
    {
        conn_.on_error([&](stompconn::packet p) {
            //cout() << p.get(stomptalk::header::message()) << std::endl;
            //cout() << p.payload().str() << std::endl;
            cout() << p.dump() << endl2;
        });
    }

    template<class Rep, class Period>
    void connect(const std::string& host, int port,
                 std::chrono::duration<Rep, Period> timeout)
    {
        cout([&]{
            std::string text;
            text += "connect to: "sv;
            text += host;
            if (port)
                text += ' ' + std::to_string(port);
            return text;
        });

        host_ = host;
        port_ = port;

        conn_.connect(dns_, host, port, timeout);
    }

    void on_event(short ef)
    {
        cout() << "disconnect: " << ef << std::endl;
        // любое событие приводик к закрытию сокета
//        queue_.once(std::chrono::seconds(5), [&](...){
//            connect(host_, port_, std::chrono::seconds(20));
//        });
    }

    void on_connect()
    {
        stompconn::logon logon("stompdemo", "stompdemo", "123");
        logon.push(stomptalk::header::heart_beat(2000, 2000));
        conn_.send(std::move(logon),
            std::bind(&peer4::on_logon, this, std::placeholders::_1));
    }

    void on_logon(stompconn::packet logon)
    {
        if (logon)
        {
            cout() << logon.session() << std::endl;

            {
                stompconn::send frame("/exchange/test/@12345");
                frame.push(stomptalk::header::message_ttl("600000"));
                frame.payload(btpro::buffer("sometext"));
                conn_.send(std::move(frame), [](stompconn::packet p){
                    cout() << p.dump() << endl2;
                });
            }
        }
        else
        {
            cout() << logon.dump() << endl2;
            on_event(BEV_EVENT_EOF);
        }
    }
};

int main()
{
    try
    {
        // инициализация wsa
        btpro::startup();
        btpro::dns dns;
        auto queue = create_queue();
        dns.create(queue, btpro::dns_initialize_nameservers);

        // эхо
        //peer1 p1(queue, dns);
        // запись транзакций
        //peer2 p2(queue);
        // маршруты
        //peer3 p3(queue, dns);
        rpc rpc1(queue, dns, "a1", "a2");
        rpc rpc2(queue, dns, "a2", "a1");
        //peer4 p4(queue, dns);

#ifndef WIN32
        auto f = [&](auto...) {
            cerr() << "stop!"sv << std::endl;
            queue.loop_break();
            return 0;
        };

        btpro::evs sint;
        sint.create(queue, SIGINT, EV_SIGNAL|EV_PERSIST, f);
        sint.add();

        btpro::evs sterm;
        sterm.create(queue, SIGTERM, EV_SIGNAL|EV_PERSIST, f);
        sterm.add();
#endif // _WIN32

        //p1.connect("bigtux.hosts", 61613, std::chrono::seconds(20));
        //p2.connect_localhost(std::chrono::seconds(20));
        //p3.connect("bigtux.hosts", 61613, std::chrono::seconds(20));
        //p4.connect("bigtux.hosts", 61613, std::chrono::seconds(20));


//        rpc1.connect("bigtux.hosts", 61613, std::chrono::seconds(20));
//        rpc2.connect("bigtux.hosts", 61613, std::chrono::seconds(20));
//        rpc1.connect("localhost", 14889, std::chrono::seconds(20));
//        rpc2.connect("localhost", 14889, std::chrono::seconds(20));
        rpc1.connect("127.0.0.1", 61613, std::chrono::seconds(20));
        rpc2.connect("127.0.0.1", 61613, std::chrono::seconds(20));

        queue.dispatch();
    }
    catch (const std::exception& e)
    {
        cerr() << e.what() << std::endl;
    }

    return 0;
}
