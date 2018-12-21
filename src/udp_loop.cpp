#include "udp_loop.hpp"
#include <utp/socket.hpp>
#include <boost/asio/steady_timer.hpp>

#include <iostream>

using namespace utp;
using namespace std;

struct udp_loop::ticker_type : public enable_shared_from_this<ticker_type> {
    bool _stopped = false;
    asio::steady_timer _timer;
    function<void()> _on_tick;

    ticker_type(asio::io_service& ios, function<void()> on_tick)
        : _timer(ios)
        , _on_tick(move(on_tick))
    {}

    void start() {
        _timer.expires_from_now(chrono::milliseconds(500));
        _timer.async_wait([this, self = shared_from_this()]
                          (const sys::error_code&) {
                              if (_stopped) return;
                              _on_tick();
                              if (_stopped) return;
                              start();
                          });
    }

    void stop() {
        _timer.cancel();
        _stopped = true;
    }
};

uint64 udp_loop::callback_log(utp_callback_arguments* a)
{
    cerr << "LOG: " << a->socket << " " << a->buf << endl;
    return 0;
}

uint64 udp_loop::callback_sendto(utp_callback_arguments* a)
{
    udp_loop* self = (udp_loop*) utp_context_get_userdata(a->context);

    sys::error_code ec;

    self->_socket.send_to( asio::buffer(a->buf, a->len)
                         , util::to_endpoint(*a->address)
                         , 0
                         , ec);

    // The libutp library sometimes calls this function even after the last
    // socket holding this udp_loop has received an EOF and closed.
    // TODO: Should this be fixed in libutp?
    if (ec && ec == asio::error::bad_descriptor) {
        return 0;
    }

    if (ec && ec != asio::error::would_block) {
        assert(0 && "TODO");
    }

    return 0;
}

uint64 udp_loop::callback_on_error(utp_callback_arguments*)
{
    return 0;
}

uint64 udp_loop::callback_on_state_change(utp_callback_arguments* a)
{
    auto socket = (utp::socket_impl*) utp_get_userdata(a->socket);

    if (!socket) {
        // The utp::socket_impl has detached from this utp_socket
        return 0;
    }

    switch(a->state) {
        case UTP_STATE_CONNECT:
            socket->on_connect();
            break;

        case UTP_STATE_WRITABLE:
            socket->on_writable();
            break;

        case UTP_STATE_EOF:
            socket->on_eof();
            break;

        case UTP_STATE_DESTROYING:
            socket->on_destroy();
            break;
    }

    return 0;
}

uint64 udp_loop::callback_on_read(utp_callback_arguments* a)
{
    auto socket = (utp::socket_impl*) utp_get_userdata(a->socket);
    assert(socket);
    socket->on_receive(a->buf, a->len);

    return 0;
}

uint64 udp_loop::callback_on_firewall(utp_callback_arguments* a)
{
    auto* self = (udp_loop*) utp_context_get_userdata(a->context);

    if (self->_accepting_sockets.empty()) {
        return 1;
    }

    return 0;
}

uint64 udp_loop::callback_on_accept(utp_callback_arguments* a)
{
    auto* self = (udp_loop*) utp_context_get_userdata(a->context);

    if (self->_accepting_sockets.empty()) return 0;

    auto& s = self->_accepting_sockets.front();
    self->_accepting_sockets.pop_front();

    s.on_accept(a->socket);

    return 0;
}

udp_loop::udp_loop(asio::ip::udp::socket socket)
    : _socket(std::move(socket))
    , _utp_ctx(utp_init(2 /* version */))
{
    // TODO: Throw?
    assert(_utp_ctx);

    if (!_socket.non_blocking()) {
        _socket.non_blocking(true);
    }

    utp_context_set_userdata(_utp_ctx, this);

    //utp_set_callback(_utp_ctx, UTP_LOG,             &callback_log);
    utp_set_callback(_utp_ctx, UTP_SENDTO,          &callback_sendto);
    utp_set_callback(_utp_ctx, UTP_ON_ERROR,        &callback_on_error);
    utp_set_callback(_utp_ctx, UTP_ON_STATE_CHANGE, &callback_on_state_change);
    utp_set_callback(_utp_ctx, UTP_ON_READ,         &callback_on_read);
    utp_set_callback(_utp_ctx, UTP_ON_FIREWALL,     &callback_on_firewall);
    utp_set_callback(_utp_ctx, UTP_ON_ACCEPT,       &callback_on_accept);
}

void udp_loop::start()
{
    start_reading();

    _ticker = make_shared<ticker_type>(_socket.get_io_service(), [this] {
            assert(_utp_ctx);
            if (!_utp_ctx) return;
            utp_check_timeouts(_utp_ctx);
        });

    _ticker->start();
}

void udp_loop::stop()
{
    _socket.close();

    _ticker->stop();
    _ticker = nullptr;
}

void udp_loop::start_reading()
{
    if (!_socket.available()) {
        utp_issue_deferred_acks(_utp_ctx);
    }

    _socket.async_receive_from( asio::buffer(_rx_buffer)
                              , _rx_endpoint
                              , [this, self = shared_from_this()]
                                (const sys::error_code& ec, size_t size)
                                {
                                    on_read(ec, size);
                                });
}

void udp_loop::on_read(const sys::error_code& ec, size_t size)
{
    if (ec) {
        utp_issue_deferred_acks(_utp_ctx);
        return;
    }

    sockaddr src_addr = util::to_sockaddr(_rx_endpoint);

    bool handled = utp_process_udp( _utp_ctx
                                  , (unsigned char*) _rx_buffer.data()
                                  , size
                                  , &src_addr
                                  , sizeof(src_addr));

    if (!handled) {
        // TODO: Add some way to the user to handle these packets.
        std::cerr << "Unhandled UDP packet" << std::endl;
    }

    if (!_socket.is_open()) {
        return;
    }

    start_reading();
}

asio::io_service& udp_loop::get_io_service()
{
    return _socket.get_io_service();
}
