#include <asio_utp/socket.hpp>
#include <asio_utp/log.hpp>
#include "service.hpp"
#include "context.hpp"
#include "util.hpp"
#include "weak_from_this.hpp"

#include <utp.h>

using namespace std;
using namespace asio_utp;

socket_impl::socket_impl(socket* owner, std::shared_ptr<context> ctx)
    : _ex(owner->get_executor())
    , _owner(owner)
    , _context(std::move(ctx))
    , _id(_context->id().generate_socket_id())
{
    if (_debug) {
        log(_id, " socket_impl::socket_impl()");
    }
}


void socket_impl::on_connect()
{
    post_op(_connect_handler, "connect", sys::error_code());
}


void socket_impl::on_receive(const unsigned char* buf, size_t size)
{
    if (_debug) {
        log( _id, " socket_impl::on_receive "
           , "_recv_handler:", bool(_recv_handler), " "
           , "size:", size);
    }

    using asio::const_buffer;
    using asio::mutable_buffer;
    using asio::buffer_size;
    using asio::buffer_copy;

    if (!_recv_handler) {
        _rx_buffer_queue.push_back({buf, buf+size});
        return;
    }

    assert(_rx_buffer_queue.empty());

    const_buffer src(buf, size);

    size_t total = 0;

    for (mutable_buffer dst : _rx_buffers) {
        size_t c = buffer_copy(dst, src);
        src = src + c;
        total += c;

        // If the recv buffer is smaller than what we've received,
        // we need to store it for later.
        if (buffer_size(src) != 0) {
            const unsigned char* begin = static_cast<const unsigned char*>(src.data());
            const unsigned char* end   = begin + buffer_size(src);
            _rx_buffer_queue.push_back({begin, end});
            break;
        }
    }

    if (total == size) {
        utp_read_drained((utp_socket*) _utp_socket);
    }

    post_op(_recv_handler, "recv", sys::error_code(), total);
}


void socket_impl::on_accept(void* usocket)
{
    if (_debug) {
        log(_id, " socket_impl::on_accept utp_socket:", usocket);
    }

    assert(!_utp_socket);
    assert(_accept_handler);

    utp_set_userdata((utp_socket*) usocket, this);

    _utp_socket = usocket;
    dispatch_op(_accept_handler, "accept", sys::error_code());
}


template<class Handler>
void socket_impl::setup_op(Handler& target, Handler&& h, const char* dbg)
{
    _context->increment_outstanding_ops(dbg);
    target = std::move(h);
    target.exec_after([ctx = _context, dbg] { ctx->decrement_outstanding_ops(dbg); });
}

template<class Handler, class... Args>
void socket_impl::post_op(Handler& h, const char* dbg, const sys::error_code& ec, Args... args)
{
    h.post(ec, args...);
}

template<class Handler, class... Args>
void socket_impl::dispatch_op(Handler& h, const char* dbg, const sys::error_code& ec, Args... args)
{
    h.dispatch(ec, args...);
}

void socket_impl::do_write(handler<size_t> h)
{
    if (_debug) {
        log(_id, " socket_impl::do_write");
    }

    assert(!_send_handler);

    if (!_utp_socket) {
        return h.post(asio::error::bad_descriptor, 0);
    }

    setup_op(_send_handler, std::move(h), "write");

    bool still_writable = true;

    for (auto& b : _tx_buffers) {
        while (size_t s = asio::buffer_size(b)) {
            // TODO: Use utp_writev
            auto w = utp_write( (utp_socket*) _utp_socket
                              , const_cast<void*>(b.data())
                              , s);

            assert(w >= 0);

            _bytes_sent += w;
            b = b + w;
            s = asio::buffer_size(b);

            if (size_t(w) < s) {
                still_writable = false;
                break;
            }
        }

        if (!still_writable) break;
    }

    if (still_writable) {
        post_op(_send_handler, "write", sys::error_code(), _bytes_sent);
        _bytes_sent = 0;
    }
}


void socket_impl::on_writable()
{
    if (_debug) {
        log(_id, " socket_impl::on_writable");
    }

    if (!_send_handler) return;
    do_write(std::move(_send_handler));
}

void socket_impl::do_read(handler<size_t> h)
{
    if (_debug) {
        log(_id, " socket_impl::do_read ",
            " buffer_size(_rx_buffers):", asio::buffer_size(_rx_buffers),
            " _rx_buffer_queue.size():", _rx_buffer_queue.size(),
            " buffer_size(_rx_buffer_queue):", asio::buffer_size(_rx_buffer_queue));
    }

    assert(!_recv_handler);

    if (!is_open()) {
        // User provided an empty RX buffer => post handler right a way.
        return h.post(asio::error::bad_descriptor, 0);
    }

    if (asio::buffer_size(_rx_buffers) == 0) {
        return h.post(sys::error_code(), 0);
    }

    setup_op(_recv_handler, std::move(h), "read");

    // If we haven't yet received anything, we wait. But note that if we did,
    // but the _rx_buffers is empty, then we still post the callback with zero
    // size.
    if (_rx_buffer_queue.empty()) {
        if (_got_eof) {
            close_with_error(asio::error::connection_reset);
        }
        return;
    }

    size_t s = asio::buffer_copy(_rx_buffers, _rx_buffer_queue);
    size_t r = s;

    while (r) {
        assert(!_rx_buffer_queue.empty());

        auto& buf = _rx_buffer_queue.front();

        if (r >= buf.size() - buf.consumed) {
            r -= buf.size() - buf.consumed;
            _rx_buffer_queue.erase(_rx_buffer_queue.begin());
        } else {
            buf.consumed += r;
            break;
        }
    }

    post_op(_recv_handler, "recv", sys::error_code(), s);
}


void socket_impl::do_accept(handler<> h)
{
    if (_debug) {
        log(_id, " socket_impl::do_accept");
    }

    // TODO: Which error code to call `h` with?
    assert(!_accept_handler);
    _context->add_accepting_socket(*this);

    setup_op(_accept_handler, std::move(h), "accept");
}


asio::ip::udp::endpoint socket_impl::local_endpoint() const
{
    return _context->local_endpoint();
}

asio::ip::udp::endpoint socket_impl::remote_endpoint() const
{
    assert(_utp_socket && "TODO: This should throw");
    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    int result = utp_getpeername((utp_socket*) _utp_socket, &addr, &addrlen);
    assert(!result && "TODO: This should throw");
    return util::to_endpoint(addr);
}

void socket_impl::close()
{
    if (_debug) {
        log(_id, " socket_impl::close()");
    }

    close_with_error(asio::error::operation_aborted);
}


void socket_impl::on_eof()
{
    if (_debug) {
        log(_id, " socket_impl::on_eof",
                " _send_handler:", bool(_send_handler),
                " _recv_handler:", bool(_recv_handler));
    }

    assert(!_got_eof);
    _got_eof = true;

    if (_recv_handler) {
        post_op(_recv_handler, "recv", asio::error::connection_reset, 0);
    }
}


// Called by libutp once the socket finished its termination sequence (send
// `fin`; receive `ack`; etc...)
void socket_impl::on_destroy()
{
    if (_debug) {
        log( _id, " socket_impl::on_destroy"
           , " refcount:", asio_utp::weak_from_this(this).use_count());
    }

    assert(_utp_socket);

    _utp_socket = nullptr;

    close_with_error(asio::error::connection_aborted);

    if (_decrement_close) {
        _context->decrement_outstanding_ops("close");
        _decrement_close = false;
    }
}


void socket_impl::close_with_error(const sys::error_code& ec)
{
    if (_debug) {
        log(_id, " socket_impl::close_with_error "
            "_utp_socket:", _utp_socket, " _closed:", _closed);
    }

    if (_closed) {
        assert(!_accept_handler);
        assert(!_connect_handler);
        assert(!_recv_handler);
        assert(!_send_handler);
        return;
    }

    _closed = true;

    if (_accept_handler) {
        post_op(_accept_handler, "accept", ec);
    }

    if (_connect_handler) {
        post_op(_connect_handler, "connect", ec);
    }

    if (_recv_handler) {
        post_op(_recv_handler, "recv", ec, 0);
    }

    if (_send_handler) {
        post_op(_send_handler, "send", ec, 0);
    }

    auto s = (utp_socket*) _utp_socket;

    // NOTE: If we `_got_eof` then we don't call `utp_close` because that would
    // make the uTP context start sending FIN packets to the other side, but if
    // the other side already closed the socket, then we would wait for FIN ACK
    // unnecessarily until timeout (which is quite long).
    if (s && !_got_eof) {
        // Note: Calling utp_close may trigger a call to this function again.
        utp_close(s);
        if (_owner) {
            _owner->_socket_impl = nullptr;
            _owner = nullptr;
        }
        assert(!_decrement_close);
        _context->increment_outstanding_ops("close");
        _decrement_close = true;
    }
}


socket_impl::~socket_impl()
{
    if (_debug) {
        log(_id, " socket_impl::~socket_impl()");
    }

    if (_utp_socket) {
        utp_set_userdata((utp_socket*) _utp_socket, nullptr);
    }

    close_with_error(asio::error::connection_aborted);
}


void socket_impl::do_connect(const endpoint_type& ep, handler<> h)
{
    if (_debug) {
        log(_id, " socket_impl::do_connect ep:", ep);
    }

    assert(!_utp_socket);

    setup_op(_connect_handler, std::move(h), "connect");

    sockaddr_storage addr = util::to_sockaddr(ep);

    _utp_socket = utp_create_socket(_context->get_libutp_context());
    utp_set_userdata((utp_socket*) _utp_socket, this);

    utp_connect((utp_socket*) _utp_socket, (sockaddr*) &addr, util::sockaddr_size(addr));
}
