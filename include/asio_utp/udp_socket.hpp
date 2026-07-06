#pragma once

#include <span>

#include <boost/asio/any_completion_handler.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/udp.hpp>
#include <asio_utp/detail/handler.hpp>

namespace asio_utp {

// Base class for types that behave like UDP sockets.
class abstract_udp_socket {
public:
    using endpoint_type = boost::asio::ip::udp::endpoint;
    using executor_type = AsioExecutor;

    using handler = boost::asio::any_completion_handler<void(const boost::system::error_code&, std::size_t)>;

public:
    virtual ~abstract_udp_socket();

    virtual const executor_type& get_executor() = 0;

    virtual endpoint_type local_endpoint(boost::system::error_code&) const = 0;
    endpoint_type local_endpoint() const;

    virtual bool is_open() const = 0;
    virtual void cancel(boost::system::error_code&) = 0;

    virtual std::size_t available(boost::system::error_code&) const = 0;

    // Starts asynchronous datagram receive. Invokes the handler on completion, passing it the error
    // code and the number of bytes received.
    virtual void async_receive_from(
        const std::span<boost::asio::mutable_buffer>&,
        endpoint_type&,
        handler
    ) = 0;

    // Starts asynchronous datagram send. Invoke the handler on completion, passing it the error code
    // and the number of bytes sent.
    virtual void async_send_to(
        const std::span<const boost::asio::const_buffer>&,
        const endpoint_type&,
        handler
    ) = 0;

    // Send a datagram immediatelly without blocking. If it can't be done (e.g., the underlying
    // send buffer is full), it must return immediatelly and set the
    // `boost::asio::error::would_block` error code.
    // Returns the number of bytes sent.
    virtual std::size_t immediate_send_to(
        const std::span<const boost::asio::const_buffer>&,
        const endpoint_type&,
        boost::asio::socket_base::message_flags,
        boost::system::error_code&
    ) = 0;
};

// Wrapper for `boost::asio::ip::udp::socket` which implements `abstract_udp_socket`.
class udp_socket_wrapper : public abstract_udp_socket {
public:

    explicit udp_socket_wrapper(boost::asio::ip::udp::socket inner) : _inner(std::move(inner))
    {
        if (!_inner.non_blocking()) {
            _inner.non_blocking(true);
        }
    }

    udp_socket_wrapper(udp_socket_wrapper&&) = default;
    udp_socket_wrapper& operator=(udp_socket_wrapper&&) = default;

    const executor_type& get_executor() override {
        return _inner.get_executor();
    }

    endpoint_type local_endpoint(boost::system::error_code& ec) const override {
        return _inner.local_endpoint(ec);
    }

    bool is_open() const override {
        return _inner.is_open();
    }

    void cancel(boost::system::error_code& ec) override {
        std::ignore = _inner.cancel(ec);
    }

    std::size_t available(boost::system::error_code& ec) const override {
        return _inner.available(ec);
    }

    void async_receive_from(
        const std::span<boost::asio::mutable_buffer>& buffers,
        endpoint_type& sender,
        handler handler
    ) override {
        _inner.async_receive_from(buffers, sender, std::move(handler));
    }

    void async_send_to(
        const std::span<const boost::asio::const_buffer>& buffers,
        const endpoint_type& receiver,
        handler handler
    ) override {
        _inner.async_send_to(buffers, receiver, std::move(handler));
    }

    std::size_t immediate_send_to(
        const std::span<const boost::asio::const_buffer>& buffers,
        const endpoint_type& receiver,
        boost::asio::socket_base::message_flags flags,
        boost::system::error_code& ec
    ) override {
        // NOTE: we set the socket to non-blocking mode in the constructor so this call should never
        // block.
        return _inner.send_to(buffers, receiver, flags, ec);
    }

private:

    boost::asio::ip::udp::socket _inner;
};

} // namespace asio_utp
