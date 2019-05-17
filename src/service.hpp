#pragma once

#include <boost/asio.hpp>
#include "udp_multiplexer_impl.hpp"
#include "namespaces.hpp"

namespace asio_utp {

class context;

class service : public asio::execution_context::service {
public:
    using endpoint_type = asio::ip::udp::endpoint;
    using socket_type = asio::ip::udp::socket;

public:
    static asio::io_context::id id;

    service(asio::execution_context& ctx)
        : asio::execution_context::service(ctx)
    {}

    template<class Executor>
    std::shared_ptr<::asio_utp::context>
    maybe_create_context(Executor&, const endpoint_type&);

    void erase_context(endpoint_type ep);

    template<class Executor>
    std::shared_ptr<udp_multiplexer_impl>
    maybe_create_udp_multiplexer(Executor&, const endpoint_type&);

    void shutdown() override {}

private:
    std::map<endpoint_type, std::weak_ptr<::asio_utp::context>> _contexts;
    std::map<endpoint_type, std::weak_ptr<udp_multiplexer_impl>> _multiplexers;
};

} // namespace

#include "context.hpp"

namespace asio_utp {

template<class Executor>
inline
std::shared_ptr<::asio_utp::context>
service::maybe_create_context(Executor& ex, const endpoint_type& ep)
{
    auto i = _contexts.find(ep);

    if (i != _contexts.end()) return i->second.lock();

    auto ctx = std::make_shared<::asio_utp::context>(socket_type(ex, ep));
    _contexts[ctx->local_endpoint()] = ctx;

    return ctx;
}

inline
void service::erase_context(endpoint_type ep)
{
    _contexts.erase(ep);
}

template<class Executor>
inline
std::shared_ptr<udp_multiplexer_impl>
service::maybe_create_udp_multiplexer(Executor& ex, const endpoint_type& ep)
{
    auto i = _multiplexers.find(ep);

    if (i != _multiplexers.end()) return i->second.lock();

    auto m = std::make_shared<udp_multiplexer_impl>(socket_type(ex, ep));
    _multiplexers[m->local_endpoint()] = m;

    m->start();

    return m;
}

} // asio_utp
