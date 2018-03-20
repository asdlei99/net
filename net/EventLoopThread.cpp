//
// Created by lg on 17-4-21.
//
#include<functional>
#include <cassert>
#include"EventLoopThread.h"

namespace  net
{
    EventLoopThread::~EventLoopThread()noexcept {
        if(_th.joinable()){
            _th.join();
        }
    }

    EventLoopThread::EventLoopThread()noexcept{

    }

    void EventLoopThread::join(){
        _th.join();
    }

    void EventLoopThread::run() {
        _th=std::thread(&EventLoop::run,&_loop);
    }

    void EventLoopThread::stop() {
        _loop.stop();
    }

    EventLoop *EventLoopThread::get_loop() {
        return &_loop;
    }
}