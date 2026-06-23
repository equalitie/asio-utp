#include "id.hpp"

namespace asio_utp {

uint32_t next_multiplexer_id = 0;

MultiplexerId::MultiplexerId():
    _value(next_multiplexer_id++)
{}

} // namespace
