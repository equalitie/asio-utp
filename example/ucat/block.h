#pragma once

#include <memory>
#include <boost/asio/spawn.hpp>

class block {

#if BOOST_VERSION >= 107400
    using AsioExecutor = boost::asio::any_io_executor;
#else
    using AsioExecutor = boost::asio::executor;
#endif

public:
    block(const AsioExecutor&);
    block(const block&) = delete;
    block& operator=(const block&) = delete;

    ~block();

    void release();
    void wait(boost::asio::yield_context yield);

private:
    AsioExecutor _ex;
    std::function<void(boost::system::error_code)> _on_notify;
    bool _released = false;
};

inline
block::block(const AsioExecutor& ex)
    : _ex(ex)
{}

inline
block::~block()
{
    if (!_on_notify) return;

    boost::asio::post(_ex, [h = std::move(_on_notify)] {
            h(boost::asio::error::operation_aborted);
        });
}

inline
void block::release()
{
    _released = true;

    if (!_on_notify) return;

    boost::asio::post(_ex, [h = std::move(_on_notify)] {
            h(boost::system::error_code());
        });
}

inline
void block::wait(boost::asio::yield_context yield)
{
    namespace asio   = boost::asio;
    namespace system = boost::system;

    if (_released) return;

    asio::async_completion<decltype(yield), void(system::error_code)> c(yield);

    _on_notify = [ h = std::move(c.completion_handler)
                 , w = asio::make_work_guard(_ex)
                 ] (const system::error_code& ec) mutable {
                     h(ec);
                 };

    return c.result.get();
}
