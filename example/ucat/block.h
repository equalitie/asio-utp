#pragma once

#include <boost/asio/async_result.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <memory>
#include <boost/asio/spawn.hpp>

class block {
    using AsioExecutor = boost::asio::any_io_executor;

public:
    block(const AsioExecutor&);
    block(const block&) = delete;
    block& operator=(const block&) = delete;

    ~block();

    void release();
    void wait(boost::asio::yield_context yield);

private:
    AsioExecutor _ex;
    std::move_only_function<void(boost::system::error_code)> _on_notify;
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

    boost::asio::post(_ex, [h = std::move(_on_notify)] mutable {
            h(boost::asio::error::operation_aborted);
        });
}

inline
void block::release()
{
    _released = true;

    if (!_on_notify) return;

    boost::asio::post(_ex, [h = std::move(_on_notify)] mutable {
            h(boost::system::error_code());
        });
}

inline
void block::wait(boost::asio::yield_context yield)
{
    namespace asio   = boost::asio;
    namespace system = boost::system;

    if (_released) return;

    return asio::async_initiate<asio::yield_context, void(system::error_code)>(
        [this] (auto handler) {
            _on_notify =
                [
                    w = asio::make_work_guard(_ex),
                    h = std::move(handler)
                ]
                (boost::system::error_code ec) mutable {
                    h(ec);
                };
        },
        yield
    );
}
