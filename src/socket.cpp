#include <asio_utp/socket.hpp>
#include <asio_utp/udp_multiplexer.hpp>
#include "namespaces.hpp"
#include "socket_impl.hpp"
#include "service.hpp"

using namespace std;
using namespace asio_utp;
using AsioExecutor = asio_utp::AsioExecutor;

socket::socket(boost::asio::io_context& ioc)
    : _ex(ioc.get_executor())
    , _service(asio::use_service<service>(ioc))
{}

socket::socket(AsioExecutor ex)
    : _ex(std::move(ex))
    , _service(asio::use_service<service>(_ex.context()))
{}

void socket::bind(const endpoint_type& ep, sys::error_code& ec)
{
    if (_socket_impl) {
        // TODO: Is this the correct error code?
        ec = asio::error::already_open;
        return;
    }

    auto multiplexer = _service.maybe_create_udp_multiplexer(_ex, ep, ec);
    if (ec) return;

    _multiplexer = std::move(multiplexer);
    auto& ctx = _multiplexer->get_context();
    _socket_impl = std::shared_ptr<socket_impl>(new socket_impl(this, ctx.shared_from_this()));
    ctx.register_socket(_socket_impl);
}

void socket::bind(const udp_multiplexer& m, sys::error_code& ec)
{
    if (_socket_impl) {
        // TODO: Is this the correct error code?
        ec = asio::error::already_open;
        return;
    }

    _multiplexer = m.impl();
    auto& ctx = _multiplexer->get_context();
    _socket_impl = std::shared_ptr<socket_impl>(new socket_impl(this, ctx.shared_from_this()));
    ctx.register_socket(_socket_impl);
}

socket::socket(socket&& other)
    : _ex(std::move(other._ex))
    , _service(other._service)
    , _socket_impl(std::move(other._socket_impl))
{
    if (_socket_impl) {
        _socket_impl->_owner = this;
    }
}

asio_utp::socket& socket::operator=(socket&& other)
{
    assert(!_ex || !other._ex || _ex == other._ex);

    _ex = std::move(other._ex);
    _socket_impl = std::move(other._socket_impl);

    if (_socket_impl) {
        assert(other._socket_impl->_owner);
        _socket_impl->_owner = this;
    }

    return *this;
}

boost::asio::ip::udp::endpoint socket::local_endpoint() const
{
    assert(_socket_impl); // TODO: throw
    return _socket_impl->local_endpoint();
}

boost::asio::ip::udp::endpoint socket::remote_endpoint() const
{
    assert(_socket_impl); // TODO: throw
    return _socket_impl->remote_endpoint();
}

bool socket::is_open() const {
    return _socket_impl && _socket_impl->is_open();
}

void socket::close()
{
    if (!_socket_impl) return;

    if (_socket_impl->is_open()) {
        _socket_impl->close();
    }

    _socket_impl = nullptr;
    _multiplexer = nullptr;
}

socket::~socket()
{
    close();
}

void socket::do_connect(const endpoint_type& ep_, handler<>&& h)
{
    if (!_socket_impl) {
        return h.post(asio::error::bad_descriptor);
    }

    auto ep = ep_;

    // Libutp can't connect to an unspecified IP address. But it seems
    // (https://tools.ietf.org/html/rfc5735#section-3) it's OK if we connect to
    // "this" host instead.
    if (ep.address().is_unspecified()) {
        if (ep.address().is_v4()) {
            ep.address(asio::ip::address_v4::loopback());
        } else {
            ep.address(asio::ip::address_v6::loopback());
        }
    }

    _socket_impl->do_connect(ep, std::move(h));
}

void socket::do_accept(handler<>&& h)
{
    if (!_socket_impl) {
        return h.post(asio::error::bad_descriptor);
    }

    _socket_impl->do_accept(std::move(h));
}

void socket::do_write(handler<size_t>&& h)
{
    if (!_socket_impl) {
        return h.post(asio::error::bad_descriptor, 0);
    }

    _socket_impl->do_write(std::move(h));
}

void socket::do_read(handler<size_t>&& h)
{
    if (!_socket_impl) {
        return h.post(asio::error::bad_descriptor, 0);
    }

    _socket_impl->do_read(std::move(h));
}

std::vector<boost::asio::const_buffer>* socket::tx_buffers()
{
    if (!_socket_impl) return nullptr;
    return &_socket_impl->_tx_buffers;
}

std::vector<boost::asio::mutable_buffer>* socket::rx_buffers()
{
    if (!_socket_impl) return nullptr;
    return &_socket_impl->_rx_buffers;
}
