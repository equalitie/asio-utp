#pragma once

#include <map>
#include <boost/asio.hpp>
#include "asio_utp/udp_socket.hpp"
#include "namespaces.hpp"

namespace asio_utp {

class context;
class udp_multiplexer_impl;

class service : public asio::execution_context::service {
public:
    using endpoint_type = asio::ip::udp::endpoint;
    using socket_type = asio::ip::udp::socket;

public:
    static asio::io_context::id id;

    service(asio::execution_context& ctx)
        : asio::execution_context::service(ctx)
    {}

    void erase_context(endpoint_type ep);

    template<class Executor>
    std::shared_ptr<udp_multiplexer_impl>
    maybe_create_udp_multiplexer(Executor&, const endpoint_type&, sys::error_code& ec);

    void erase_multiplexer(endpoint_type ep);

    void shutdown() override {}

    ~service();

private:
    std::map<endpoint_type, std::weak_ptr<udp_multiplexer_impl>> _multiplexers;

    bool _debug = false;
};

} // namespace

#include "udp_multiplexer_impl.hpp"
#include "context.hpp"

namespace asio_utp {

template<class Executor>
inline
std::shared_ptr<udp_multiplexer_impl>
service::maybe_create_udp_multiplexer(Executor& ex, const endpoint_type& ep, sys::error_code& ec)
{
    if (_debug) {
        log("maybe_create_udp_multiplexer ", ep, " ", _multiplexers.size());
    }

    auto i = _multiplexers.find(ep);

    if (i != _multiplexers.end()) return i->second.lock();

    socket_type socket(ex);
    socket.open(ep.protocol());
    socket.bind(ep, ec);

    if (ec) return nullptr;

    auto m = udp_multiplexer_impl::create(std::make_unique<udp_socket_wrapper>(std::move(socket)));
    _multiplexers[m->local_endpoint()] = m;

    return m;
}

inline
void service::erase_multiplexer(endpoint_type ep)
{
    auto i = _multiplexers.find(ep);
    if (i == _multiplexers.end()) {
        return;
    }

    if (_debug) {
        log("erase_multiplexer ", ep, " ", _multiplexers.size());
    }

    _multiplexers.erase(i);
}

inline service::~service()
{
    if (_debug) {
        log("~service");
        assert(_multiplexers.empty());
    }
}

} // asio_utp
