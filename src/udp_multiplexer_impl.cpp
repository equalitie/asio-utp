#include "asio_utp/udp_socket.hpp"
#include "service.hpp"

namespace asio_utp {

std::shared_ptr<udp_multiplexer_impl> udp_multiplexer_impl::create(std::unique_ptr<abstract_udp_socket> s)
{
    auto m = std::make_shared<udp_multiplexer_impl>(std::move(s));
    auto ctx = std::make_shared<::asio_utp::context>(m);
    m->_context = std::move(ctx);
    return m;
}

udp_multiplexer_impl::udp_multiplexer_impl(std::unique_ptr<abstract_udp_socket> s)
    : _udp_socket(std::move(s))
    , _state(std::make_shared<State>())
{
    if (_debug) {
        log(this, " udp_multiplexer_impl(", _udp_socket->local_endpoint(), ")");
    }
}

void udp_multiplexer_impl::register_recv_handler(recv_entry& e)
{
    e.multiplexer = asio_utp::weak_from_this(this);
    _recv_handlers.push_back(e);

    if (!_is_receiving) {
        start_receiving();
    }
}

void udp_multiplexer_impl::on_recv_entry_unlinked()
{
    if (_is_receiving && _recv_handlers.empty()) {
        // We need to do this to prevent this multiplexer from blocking
        // in the io_context.run function.

        // TODO: Unfortunately this won't work on Windows XP.
        // See "Remarks" section in Boost.Asio documentation for
        // basic_datagram_socket::cancel function.
        // Another drawback is that this will cancel other async
        // operations as well.
        //
        // A workaround I can think of right now is to either find
        // out whether it is possible to invoke async_receive_from
        // such that it doesn't block io_context.run or have another
        // socket (or perhaps the same one?) send this socket a
        // message to release from async_receive_from.
        if (_udp_socket->is_open()) {
            sys::error_code ec;
            _udp_socket->cancel(ec);
            assert(!ec);
        }
    }
}

void udp_multiplexer_impl::start_receiving()
{
    assert(!_is_receiving);
    _is_receiving = true;

    auto wself = asio_utp::weak_from_this(this);

    if (_debug) {
        log(_id, " udp_multiplexer_impl::start_receiving");
    }

    _udp_socket->async_receive_from(
        std::span(&_state->rx_buffer, 1),
        _state->rx_endpoint,
        [&, wself, s = _state] (const sys::error_code& ec, size_t size) {
            if (_debug) {
                log(_id, " udp_multiplexer_impl::start_receiving on receive ", ec.message());
            }

            if (auto self = wself.lock()) {
                assert(_is_receiving);

                bool canceled = ec == asio::error::operation_aborted
                    && _udp_socket->is_open();

                if (!canceled) {
                    flush_handlers(ec, size);
                }

                _is_receiving = false;

                if (!_recv_handlers.empty()) {
                    start_receiving();
                }
            }
        }
    );
}

void udp_multiplexer_impl::flush_handlers(const sys::error_code& ec, size_t size)
{
    if (_debug) {
        log(_id, " udp_multiplexer::flush_handlers "
            "ec:", ec.message(), " size:", size, " from:", _state->rx_endpoint);
        if (!ec) {
            log(_id, "    ", to_hex((uint8_t*) _state->rx_buffer.data(), size));
        }
    }

    if (ec) size = 0;

    auto recv_handlers = std::move(_recv_handlers);

    while (!recv_handlers.empty()) {
        auto e = recv_handlers.front();
        recv_handlers.pop_front();
        assert(e.handler);

        e.handler(
            ec,
            _state->rx_endpoint,
            static_cast<const uint8_t*>(_state->rx_buffer.data()),
            size
        );
    }
}

std::size_t udp_multiplexer_impl::send_to( const std::vector<asio::const_buffer>& buffers
                                         , const endpoint_type& destination
                                         , asio::socket_base::message_flags flags
                                         , sys::error_code& ec)
{
    if (_debug) {
        log(_id, " udp_multiplexer::send_to ", destination);
        for (auto b : buffers) {
            log(_id, "    ", to_hex((uint8_t*)b.data(), b.size()));
        }
    }

    size_t sent = _udp_socket->immediate_send_to(buffers, destination, flags, ec);

    _send_to_signal(buffers, sent, destination, ec);

    return sent;
}

udp_multiplexer_impl::on_send_to_connection udp_multiplexer_impl::on_send_to(std::function<on_send_to_handler> handler)
{
    return _send_to_signal.connect(std::move(handler));
}

size_t udp_multiplexer_impl::available(sys::error_code& ec) const
{
    return _udp_socket->available(ec);
}

udp_multiplexer_impl::~udp_multiplexer_impl() {
    if (_debug) {
        log(_id, " ~udp_multiplexer_impl");
    }

    auto& s = asio::use_service<service>(_udp_socket->get_executor().context());
    s.erase_multiplexer(local_endpoint());
    _context->close();
}

std::string udp_multiplexer_impl::to_hex(uint8_t* data, size_t size)
{
    std::stringstream ss;
    static const char chs[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
        auto ch = data[i];
        ss << chs[(ch >> 4) & 0xf] << chs[ch & 0xf];
    }
    return ss.str();
}

void udp_multiplexer_impl::recv_entry::unlink() {
    auto m = multiplexer.lock();
    if (!m) return;

    if (hook.is_linked()) {
        hook.unlink();
        m->on_recv_entry_unlinked();
    }
}

udp_multiplexer_impl::recv_entry::~recv_entry()
{
    unlink();
}

} // asio_utp
