#pragma once

#include <memory>
#include <ostream>
#include <cstdint>

namespace asio_utp {

class SocketId {
public:
    SocketId(uint32_t value, uint32_t multiplexer_id) :
        _value(value),
        _multiplexer_id(multiplexer_id)
    {}

    SocketId(const SocketId&) = default;

    friend std::ostream& operator<<(std::ostream& os, const SocketId& self) {
        return os << "m(" << self._multiplexer_id << ")/s(" << self._value << ")";
    }

private:
    uint32_t _value;
    uint32_t _multiplexer_id;
};

class MultiplexerId {
public:
    MultiplexerId();

    MultiplexerId(const MultiplexerId&) = default;

    SocketId generate_socket_id() {
        return SocketId((*_next_socket_id)++, _value);
    }

    friend std::ostream& operator<<(std::ostream& os, const MultiplexerId& self) {
        return os << "m(" << self._value << ")";
    }

private:
    uint32_t _value;
    std::shared_ptr<uint32_t> _next_socket_id = std::make_shared<uint32_t>(0);
};

} // namespace
