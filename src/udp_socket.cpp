#include <asio_utp/udp_socket.hpp>

namespace asio_utp {

abstract_udp_socket::~abstract_udp_socket()
{}

abstract_udp_socket::endpoint_type abstract_udp_socket::local_endpoint() const {
    boost::system::error_code ec;
    auto ep = local_endpoint(ec);

    if (ec) {
        throw boost::system::system_error(ec);
    } else {
        return ep;
    }
}

}
