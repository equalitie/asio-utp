#pragma once

#include <boost/asio/ip/udp.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include "detail/handler.hpp"

namespace asio_utp {

class service;
class socket_impl;
class udp_multiplexer;
class udp_multiplexer_impl;

class socket {
public:
    using endpoint_type = boost::asio::ip::udp::endpoint;
    using executor_type = boost::asio::any_io_executor;
    using AsioExecutor = asio_utp::AsioExecutor;

public:
    socket(const socket&) = delete;
    socket& operator=(const socket&) = delete;

    socket(socket&&);
    socket& operator=(socket&&);

    socket(AsioExecutor);

    void bind(const endpoint_type&, boost::system::error_code&);

    void bind(const udp_multiplexer&, boost::system::error_code&);

    template<typename CompletionToken>
    auto async_connect(const endpoint_type&, CompletionToken&&);

    template<typename CompletionToken>
    auto async_accept(CompletionToken&&);

    template< typename ConstBufferSequence
            , typename CompletionToken>
    auto async_write_some(const ConstBufferSequence&, CompletionToken&&);

    template< typename MutableBufferSequence
            , typename CompletionToken>
    auto async_read_some(const MutableBufferSequence&, CompletionToken&&);

    endpoint_type local_endpoint() const;

    endpoint_type remote_endpoint() const;

    bool is_open() const;

    void close();

    AsioExecutor get_executor()
    {
        return _ex;
    }

    // For debugging only
    void* pimpl() const { return _socket_impl.get(); }

    ~socket();

private:
    void do_connect(const endpoint_type&, handler<>&&);
    void do_accept (handler<>&&);
    void do_write  (handler<size_t>&&);
    void do_read   (handler<size_t>&&);

    std::vector<boost::asio::const_buffer>* tx_buffers();
    std::vector<boost::asio::mutable_buffer>* rx_buffers();

private:
    friend class socket_impl;
    AsioExecutor _ex;
    service& _service;
    // `_socket_impl` is shared with the `context`.
    std::shared_ptr<socket_impl> _socket_impl;
    // `_multiplexer` may be shared with multiple other `socket`s or with
    // `udp_multiplexer`s.
    std::shared_ptr<udp_multiplexer_impl> _multiplexer;
};

template<typename CompletionToken>
inline
auto socket::async_connect(const endpoint_type& ep, CompletionToken&& token)
{
    auto init = [&](auto completion_handler) {
        do_connect(ep, {get_executor(), std::move(completion_handler)});
    };

    return boost::asio::async_initiate<
        CompletionToken,
        void(boost::system::error_code)
      >(init, token);
}

template<typename CompletionToken>
inline
auto socket::async_accept(CompletionToken&& token)
{
    auto init = [&](auto completion_handler) {
        do_accept({get_executor(), std::move(completion_handler)});
    };

    return boost::asio::async_initiate<
        CompletionToken,
        void(boost::system::error_code)
    >(init, token);
}

template< typename ConstBufferSequence
        , typename CompletionToken>
inline
auto socket::async_write_some( const ConstBufferSequence& bufs
                             , CompletionToken&& token)
{
    if (auto txb = tx_buffers()) {
        txb->clear();

        std::copy( boost::asio::buffer_sequence_begin(bufs)
                 , boost::asio::buffer_sequence_end(bufs)
                 , std::back_inserter(*txb));
    }

    auto init = [&](auto completion_handler) {
        do_write({get_executor(), std::move(completion_handler)});
    };

    return boost::asio::async_initiate<
        CompletionToken,
        void(boost::system::error_code, size_t)
    >(init, token);
}

template< typename MutableBufferSequence
        , typename CompletionToken>
inline
auto socket::async_read_some( const MutableBufferSequence& bufs
                            , CompletionToken&& token)
{
    if (auto rxb = rx_buffers()) {
        rxb->clear();

        std::copy( boost::asio::buffer_sequence_begin(bufs)
                 , boost::asio::buffer_sequence_end(bufs)
                 , std::back_inserter(*rxb));
    }
    auto init = [&](auto completion_handler){
        do_read({get_executor(), std::move(completion_handler)});
    };
    return boost::asio::async_initiate<
        CompletionToken,
        void(boost::system::error_code, size_t)
    >(init, token);
}

} // namespace
