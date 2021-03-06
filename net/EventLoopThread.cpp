#include<functional>
#include <cassert>
#include"EventLoopThread.h"

namespace net
{
    EventLoopThread::~EventLoopThread()noexcept
    {
        if (_th.joinable()) {
            _th.join();
        }
    }

    EventLoopThread::EventLoopThread()noexcept = default;

    void EventLoopThread::join()
    {
        assert(_th.joinable());
        _th.join();
    }

    void EventLoopThread::run()
    {
        _th = std::thread(&EventLoopThread::thread_fn, this);
    }

    void EventLoopThread::stop()
    {
        _loop.stop();
    }

    EventLoop *EventLoopThread::loop()
    {
        return &_loop;
    }

    void EventLoopThread::thread_fn()
    {
        _loop.reset_thread_id();
        _loop.run();
    }

    void EventLoopThread::stop_and_join()
    {
        stop();
        join();
    }
}