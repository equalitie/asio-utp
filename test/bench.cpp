#include <iostream>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/utility/string_view.hpp>
#include <utp.hpp>

using namespace std;
namespace asio = boost::asio;
namespace rnd = boost::random;
using asio::ip::tcp;
using asio::ip::udp;
using Clock = std::chrono::steady_clock;
using string_view = boost::string_view;

enum class Type { client, server };

// TODO: If the `utp` namespace was not a namespace but was a class instead (as
// is the case with `asio::tcp` and `asio::udp`, we wouldn't need this struct.
struct Utp {
    using endpoint = udp::endpoint;
    using socket = utp::socket;
};

float seconds(Clock::duration d) {
    using namespace std::chrono;
    return duration_cast<milliseconds>(d).count() / 1000.f;
}

template<class Proto>
typename Proto::endpoint parse_endpoint(const string_view s)
{
    auto pos = s.find(':');

    if (pos == s.npos) {
        stringstream ss;
        ss << "Failed to parse endpoint \"" << s << "\"";
        throw runtime_error(ss.str());
    }

    auto addr = asio::ip::address::from_string(s.substr(0, pos).to_string());
    uint16_t port = std::atoi(s.substr(pos+1).data());
    return {addr, port};
}

struct Handshake {
    uint16_t seed;
    uint32_t size;
};

template<typename Socket>
Handshake handshake(Socket& s, Type type, asio::yield_context yield)
{
    uint16_t seed = 0;

    if (type == Type::client) {
        rnd::mt19937 rng(std::time(0));
        rnd::uniform_int_distribution<> byte(0, numeric_limits<uint16_t>::max());
        seed = byte(rng);

        asio::async_write(s, asio::buffer(&seed, sizeof(seed)), yield);
    }
    else {
        asio::async_read(s, asio::buffer(&seed, sizeof(seed)), yield);
    }

    //return {seed, 1024*1024*8 };
    return {seed, 5000000 };
}

template<typename Socket>
void receive(Socket& s, Type type, asio::yield_context yield)
{
    auto h = handshake(s, type, yield);
    rnd::mt19937 rng(h.seed);
    boost::random::uniform_int_distribution<> random_byte(0, 255);

    size_t to_receive = h.size;
    size_t received = 0;

    vector<uint8_t> buffer(to_receive);

    auto start = Clock::now();

    while (received < to_receive) {
        size_t size = s.async_read_some(asio::buffer(buffer), yield);

        if (received + size > to_receive) {
            throw std::runtime_error("Received more than was supposed to");
        }

        for (size_t i = 0; i < size; ++i) {
            uint8_t exp = random_byte(rng);

            if (buffer[i] != exp) {
                stringstream ss;
                ss << "Expected " << int(exp) << " but received "
                    << int(buffer[i]) << " on byte #" << (received + i);
                throw runtime_error(ss.str());
            }
        }

        received += size;
        cout << "Received: " << received << " Bytes" << endl;
    }

    cout << "Took: " << seconds(Clock::now() - start) << "s" << endl;

    asio::steady_timer t(s.get_io_service());
    t.expires_from_now(chrono::seconds(15));
    t.async_wait(yield);
}

template<typename Socket>
void send(Socket& s, Type type, asio::yield_context yield)
{
    auto h = handshake(s, type, yield);
    rnd::mt19937 rng(h.seed);
    boost::random::uniform_int_distribution<> random_byte(0, 255);

    size_t to_send = h.size;

    vector<uint8_t> buffer(to_send);

    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = random_byte(rng);
    }

    auto buf = asio::buffer(buffer);

    auto start = Clock::now();

    while (buf.size()) {
        size_t size = asio::async_write(s, buf, yield);
        buf += size;
        cout << "wrote " << size << " bytes " << buf.size() << endl;
    }

    cout << "Took: " << seconds(Clock::now() - start) << "s" << endl;
}

template<typename Proto> struct Async;

template<> struct Async<tcp> {
    static
    tcp::socket accept( asio::io_context& ioc
                      , string_view local_ep_s
                      , asio::yield_context yield)
    {
        auto local_ep = parse_endpoint<tcp>(local_ep_s);
        tcp::acceptor acceptor(ioc, local_ep);
    
        tcp::socket socket(ioc);
        acceptor.async_accept(socket, yield);
    
        return socket;
    }

    static
    tcp::socket connect( asio::io_context& ioc
                       , string_view remote_ep_s
                       , asio::yield_context yield)
    {
        auto remote_ep = parse_endpoint<tcp>(remote_ep_s);
        tcp::socket socket(ioc, {asio::ip::address_v4::any(), 0});
        socket.async_connect(remote_ep, yield);
        return socket;
    }
};

template<> struct Async<Utp> {
    static
    utp::socket accept( asio::io_context& ioc
                      , string_view local_ep_s
                      , asio::yield_context yield)
    {
        auto local_ep = parse_endpoint<Utp>(local_ep_s);
    
        utp::socket socket(ioc);
        socket.bind(local_ep);
        socket.async_accept(yield);
    
        return socket;
    }
   
    static
    utp::socket connect( asio::io_context& ioc
                       , string_view remote_ep_s
                       , asio::yield_context yield)
    {
        auto remote_ep = parse_endpoint<Utp>(remote_ep_s);
        utp::socket socket(ioc);
        socket.bind({asio::ip::address_v4::any(), 0});
        socket.async_connect(remote_ep, yield);
        return socket;
    }
};

template<class Proto>
void server( asio::io_context& ioc
           , string_view local_ep_s
           , asio::yield_context yield)
{
    cout << "Accepting..." << endl;
    auto socket = Async<Proto>::accept(ioc, local_ep_s, yield);
    cout << "Receiving..." << endl;
    receive(socket, Type::server, yield);
    cout << "Done" << endl;
}

template<class Proto>
void client( asio::io_context& ioc
           , string_view remote_ep_s
           , asio::yield_context yield)
{
    cout << "Connecting..." << endl;
    auto socket = Async<Proto>::connect(ioc, remote_ep_s, yield);
    cout << "Sending..." << endl;
    send(socket, Type::client, yield);
    cout << "Done" << endl;

    // TODO: There may still be some outbound data at this point.  If we
    // destroy the socket here, those data will never arrive to the
    // destination. We need some flush mechanism.
    asio::steady_timer t(ioc);
    t.expires_from_now(chrono::seconds(30));
    t.async_wait(yield);
    cout << "Done wait" << endl;
}


void usage(const char* app, const char* what = nullptr) {
    if (what) {
        cout << what << "\n" << endl;
    }
    cout << "Usage:" << endl;
    cout << "  " << app << " [client|server] [tcp|utp] <endpoint>" << endl;
}

int main(int argc, const char** argv)
{
    if (argc < 4) {
        usage(argv[0], "Wrong number of arguments");
        return 1;
    }

    Type type;

    if (argv[1] == string("client")) {
        type = Type::client;
    } else if (argv[1] == string("server")) {
        type = Type::server;
    } else {
        usage(argv[0], "Error in first arg");
        return 1;
    }

    string proto = argv[2];

    if (proto != "tcp" && proto != "utp") {
        usage(argv[0], "Wrong protocol");
        return 1;
    }

    string endpoint = argv[3];

    try {
        asio::io_context ioc(1);

        asio::spawn(ioc, [&] (asio::yield_context yield) {
                if (proto == "tcp") {
                    if (type == Type::client) {
                        client<tcp>(ioc, endpoint, yield);
                    } else {
                        server<tcp>(ioc, endpoint, yield);
                    }
                }
                else /* proto == utp */ {
                    if (type == Type::client) {
                        client<Utp>(ioc, endpoint, yield);
                    } else {
                        server<Utp>(ioc, endpoint, yield);
                    }
                }
            });

        ioc.run();
    }
    catch (std::exception& e) {
        cout << e.what() << endl;
    }
}
