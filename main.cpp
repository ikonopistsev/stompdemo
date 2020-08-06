#include "btpro/evcore.hpp"
#include "btpro/evstack.hpp"
#include "btpro/uri.hpp"
#include "btdef/date.hpp"
#include "btpro/tcp/bevfn.hpp"
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


template <std::size_t N>
constexpr auto mkview(const char (&text)[N]) noexcept
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
    cout() << mkview("libevent-")
           << btpro::queue::version()
           << mkview(" - ");

    btpro::config conf;
    for (auto& i : conf.supported_methods())
        std::cout << i << ' ';
    std::endl(std::cout);

#ifndef _WIN32
    conf.require_features(EV_FEATURE_ET|EV_FEATURE_O1);
#endif //
    btpro::queue q;
    q.create(conf);
    return q;
}

class peer
{
    typedef stompconn::connection connection_type;

    btpro::queue_ref queue_;
    btpro::dns_ref dns_;
    std::string host_{};
    int port_{};

    connection_type conn_{ queue_,
        std::bind(&peer::on_event, this, std::placeholders::_1),
        std::bind(&peer::on_connect, this)
    };

public:
    peer(btpro::queue_ref queue, btpro::dns_ref dns)
        : queue_(queue)
        , dns_(dns)
    {   }

    template<class Rep, class Period>
    void connect(const std::string& host, int port,
                 std::chrono::duration<Rep, Period> timeout)
    {
        cout([&]{
            std::string text;
            text += mkview("connect to: ");
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
        conn_.logon(std::move(logon),
            std::bind(&peer::on_logon, this, std::placeholders::_1));
    }

    void on_logon(stompconn::packet logon)
    {
        cout() << logon.payload() << endl2;
        if (logon)
        {
            stompconn::subscribe subs("/queue/mt4_trades",
                [&](stompconn::packet p) {
                    //cout() << p.dump() << endl2;
                    if (p)
                    {
                        stompconn::send send("/queue/mt4_trades");
                        send.payload(btpro::buffer(btdef::date::to_log_time()));
                        conn_.send(std::move(send), [&](stompconn::packet s){
                            if (s)
                            {
                                //cout() << s.dump() << endl2;
                            }
                            else
                            {
                                cerr() << s.payload().str() << std::endl;
                                on_event(BEV_EVENT_EOF);
                            }
                        });
                    }
                    else
                    {
                        cerr() << p.payload().str() << std::endl;
                        on_event(BEV_EVENT_EOF);
                    }
            });

            conn_.subscribe(std::move(subs), [&](stompconn::packet p){
                if (p)
                {
                    cout() << p.payload() << endl2;
                    stompconn::send send("/queue/mt4_trades");
                    send.payload(btpro::buffer(btdef::date::to_log_time()));
                    conn_.send(std::move(send), [&](stompconn::packet s){
                        if (s)
                        {
                            cout() << s.dump() << endl2;
                        }
                        else
                        {
                            cerr() << s.payload().str() << std::endl;
                            on_event(BEV_EVENT_EOF);
                        }
                    });
                }
                else
                {
                    cerr() << p.payload().str() << std::endl;
                    on_event(BEV_EVENT_EOF);
                }
            });
        }
        else
        {
            cerr() << logon.payload().str() << std::endl;
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

        peer p(queue, dns);

#ifndef WIN32
        auto f = [&](auto...) {
            cerr() << mkview("stop!") << std::endl;
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

        p.connect("127.0.0.1", 61613, std::chrono::seconds(20));

        queue.dispatch();
    }
    catch (const std::exception& e)
    {
        cerr() << e.what() << std::endl;
    }

    return 0;
}
