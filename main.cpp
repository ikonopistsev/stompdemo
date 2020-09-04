#include "btpro/evcore.hpp"
#include "btpro/evstack.hpp"
#include "btpro/uri.hpp"
#include "btdef/date.hpp"
#include "btpro/tcp/bevfn.hpp"
#include "btpro/sock_addr.hpp"
#include "btdef/ref.hpp"
#include "stomptalk/parser.hpp"
#include "stomptalk/parser_hook.hpp"
#include "stomptalk/antoull.hpp"
#include "stompconn/connection.hpp"

#include <iostream>
#include <list>
#include <string_view>
#include <functional>

#ifndef _WIN32
#include <signal.h>
#endif // _WIN32

using namespace std::literals;

template <std::size_t N>
constexpr auto sv(const char (&text)[N]) noexcept
{
    return std::string_view(text, N - 1);
}

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
inline std::ostream& cout(F fn)
{
    auto text = fn();
    return std::endl(output(std::cout)
        .write(text.data(), static_cast<std::streamsize>(text.size())));
}

template <class F>
inline std::ostream& cerr(F fn)
{
    auto text = fn();
    return std::endl(output(std::cerr)
        .write(text.data(), static_cast<std::streamsize>(text.size())));
}

btpro::queue create_queue()
{
    btpro::config conf;

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
    conf.require_features(EV_FEATURE_ET|EV_FEATURE_O1);
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
    std::size_t count_{};
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
        stompconn::logon logon("two", "max", "maxtwo");
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
            conn_.send(std::move(frame),
                std::bind(&peer1::on_logon, this, std::placeholders::_1));
        };

        auto second_send = [&, commit_transaction](...){
            stompconn::send frame("/queue/transaction_demo");
            frame.push(stomptalk::header::transaction(
                std::to_string(trasaction_id_)));
            frame.push(stomptalk::header::content_type_text_plain());
            frame.payload(btpro::buffer(conn_.create_message_id()));
            conn_.send(std::move(frame));
            queue_.once(std::chrono::seconds(30), commit_transaction);
        };

        auto first_send = [&, second_send](...){
            stompconn::send frame("/queue/transaction_demo");
            frame.push(stomptalk::header::transaction(
                std::to_string(trasaction_id_)));
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
            stompconn::begin frame(std::to_string(++trasaction_id_));
            conn_.send(std::move(frame), begin_receipt);
        };

        queue_.once(std::chrono::seconds(2), begin_transaction);
    }
};

// тест подключения подписки и отписки
class peer2
{
    typedef stompconn::connection connection_type;
    btpro::queue_ref queue_;

    connection_type conn_{ queue_,
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
                    auto sub_id = msg.get(stomptalk::header::subscription());

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

class peer3
{
    typedef stompconn::connection connection_type;

    btpro::queue_ref queue_;
    btpro::dns_ref dns_;
    std::string host_{};
    int port_{};

    connection_type conn_{ queue_,
        std::bind(&peer3::on_event, this, std::placeholders::_1),
        std::bind(&peer3::on_connect, this)
    };

public:
    peer3(btpro::queue_ref queue, btpro::dns_ref dns)
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
        conn_.send(stompconn::logon("two", "max", "maxtwo"),
            std::bind(&peer3::on_logon, this, std::placeholders::_1));
    }

    void on_logon(stompconn::packet logon)
    {
        if (logon)
        {
            cout() << logon.session() << std::endl;

            stompconn::subscribe subs("/queue/mt4_trades",
                [&](stompconn::packet p) {
                    auto content = p.payload().str();

                    if (!content.empty())
                        cout() << "RECEIVE << " << p.payload().str() << endl2;
                    else
                        cout() << "RECEIVE << " << p.dump() << endl2;

                    if (!p)
                        on_event(BEV_EVENT_EOF);

                    if (p.must_ack())
                    {
                        conn_.nack(p, [&](stompconn::packet a){
                            if (!a)
                            {
                                cout() << p.dump() << endl2;
                                on_event(BEV_EVENT_EOF);
                            }
                        });
                    }

                    // отправка сообщения о желании отключиться
                    // сервер после него отключает
                    conn_.logout([&](stompconn::packet l) {
                        cout() << l.dump() << endl2;
                    });
            });

            subs.push(stomptalk::header::ask_client_individual());
            conn_.send(std::move(subs), [&](stompconn::packet p){
                if (!p)
                {
                    cout() << p.dump() << endl2;
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
    typedef stompconn::connection connection_type;

    btpro::queue_ref queue_;
    btpro::dns_ref dns_;
    std::string host_{};
    int port_{};

    connection_type conn_{ queue_,
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
            //cout() << p.dump() << endl2;
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
        ;
        conn_.send(stompconn::logon("two", "max", "maxtwo"),
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
        peer1 p1(queue, dns);
        // запись транзакций
        //peer2 p2(queue);
        // маршруты
        //peer3 p3(queue, dns);
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

        p1.connect("localhost", 61613, std::chrono::seconds(20));
        //p2.connect_localhost(std::chrono::seconds(20));
        //p3.connect("threadtux", 61613, std::chrono::seconds(20));
        //p4.connect("threadtux", 61613, std::chrono::seconds(20));

        queue.dispatch();
    }
    catch (const std::exception& e)
    {
        cerr() << e.what() << std::endl;
    }

    return 0;
}
