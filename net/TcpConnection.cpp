//
// Created by lg on 17-4-19.
//

#include <cstring>
#include "TcpConnection.h"
#include "Buffer.h"
#include"Log.h"
#include "Socket.h"

namespace net
{

    TcpConnection::TcpConnection(uint64_t id, EventLoop *loop, int sockfd, const InetAddress &local_addr
                                 , const InetAddress &peer_add)
            : _sockfd(sockfd)
              , _id(id)
              , _loop(loop)
              , _event(loop, sockfd)
              , _status(Disconnected)
              , _local_addr(local_addr)
              , _peer_addr(peer_add)
    {
        _event.set_read_cb(std::bind(&TcpConnection::handle_read, this));
        _event.set_write_cb(std::bind(&TcpConnection::handle_write, this));
    }

    TcpConnection::~TcpConnection()noexcept
    {
        LOG_TRACE;
        Socket::close(_sockfd);
    }

    void TcpConnection::close()
    {
        Status t = Connected;
        if (_status.compare_exchange_strong(t, Disconnecting)) {
            LOG_TRACE << "fd=" << _sockfd;
            _loop->queue_in_loop(std::bind(&TcpConnection::handle_close, shared_from_this()));
        }
    }

    void TcpConnection::attach_to_loop()
    {
        _status = Connected;
        _event.enable_read();

        if (_connecting_cb) {
            _connecting_cb(shared_from_this());
        }
    }

    void TcpConnection::handle_read()
    {
        assert(_loop->in_loop_thread());

        auto r = _in_buff.read_from_fd(_sockfd);

        if (r.first > 0) {
            _message_cb(shared_from_this(), &_in_buff);
        }
        else if (r.first == 0) {
            _status = Disconnecting;
            handle_close();
        }
        else {
            errno = r.second;
            LOG_ERROR << "TcpConnection::handleRead";
            handle_error();
        }
    }

    void TcpConnection::handle_close()
    {
        assert(_loop->in_loop_thread());

        if (_status == Disconnected)
            return;

        _status = Disconnecting;
        _event.disable_all();

        TCPConnPtr conn(shared_from_this());

        if (_connecting_cb) {
            _connecting_cb(conn);
        }

        if (_close_cb) {
            _close_cb(conn);
        }
        LOG_TRACE << " fd=" << _sockfd;

        _status = Disconnected;
    }

    void TcpConnection::handle_error()
    {
        assert(_loop->in_loop_thread());

        int err = Socket::get_socket_error(_event.get_fd());
        LOG_ERROR << "TcpConnection::handleError - SO_ERROR = " << err;

        _status = Disconnecting;
        handle_close();
    }

    void TcpConnection::send(const std::string &str)
    {
        if (_status != Connected)
            return;

        if (_loop->in_loop_thread()) {
            send_in_loop(str.data(), str.size());
            return;
        }
        else {
            _loop->run_in_loop(std::bind(&TcpConnection::send_string_in_loop, shared_from_this(), str));
        }
    }

    void TcpConnection::send(const char *str, size_t len)
    {
        if (_status != Connected)
            return;

        if (_loop->in_loop_thread()) {
            send_in_loop(str, len);
            return;
        }
        else {
            _loop->run_in_loop(
                    std::bind(&TcpConnection::send_string_in_loop, shared_from_this(), std::string(str, len)));
        }
    }

    void TcpConnection::send(const Buffer *d)
    {
        send(d->get_read_ptr(), d->get_readable_size());
    }

    void TcpConnection::send_string_in_loop(const std::string &str)
    {
        send_in_loop(str.data(), str.size());
    }

    void TcpConnection::send_in_loop(const char *message, size_t len)
    {
        if (_status == Disconnected)
            return;

        ssize_t nwritten = 0;
        size_t remaining = len;
        bool write_error = false;

        if (!_event.is_write() && _out_buff.length() == 0) {
            nwritten = ::send(_sockfd, message, len, MSG_NOSIGNAL);
            if (nwritten >= 0) {
                remaining = len - nwritten;
                if (remaining == 0 && _write_complete_cb) {
                    _loop->queue_in_loop(std::bind(_write_complete_cb, shared_from_this()));
                }
            }
            else {
                int serrno = errno;
                nwritten = 0;
                if (serrno != EWOULDBLOCK) {
                    LOG_ERROR << "SendInLoop write failed errno=" << serrno << " " << strerror(serrno);
                    if (serrno == EPIPE || serrno == ECONNRESET) {
                        write_error = true;
                    }
                }
            }
        }

        if (write_error) {
            handle_error();
            return;
        }

        assert(!write_error);
        assert(remaining <= len);

        if (remaining > 0) {
            size_t old_len = _out_buff.length();
            if (old_len + remaining >= _high_level_mark
                && old_len < _high_level_mark
                && _write_high_level_cb) {
                _loop->queue_in_loop(std::bind(_write_high_level_cb, shared_from_this(), old_len + remaining));
            }

            _out_buff.append(message + nwritten, remaining);

            if (!_event.is_write()) {
                _event.enable_write();
            }
        }
    }

    void TcpConnection::handle_write()
    {
        assert(_loop->in_loop_thread());
        assert(!_event.is_add_to_loop() || _event.is_write());

        ssize_t n = ::send(_sockfd, _out_buff.get_read_ptr(), _out_buff.get_readable_size(), MSG_NOSIGNAL);
        if (n > 0) {
            _out_buff.has_read(static_cast<size_t>(n));

            if (_out_buff.get_readable_size() == 0) {
                _event.disable_write();

                if (_write_complete_cb) {
                    _loop->queue_in_loop(std::bind(_write_complete_cb, shared_from_this()));
                }
            }
        }
        else {
            int serrno = errno;

            if (serrno != EWOULDBLOCK) {
                LOG_WARN << "this=" << this << " TCPConn::HandleWrite errno=" << serrno << " " << strerror(serrno);
            }
            else {
                handle_error();
            }
        }
    }

    void TcpConnection::set_message_cb(const MessageCallback &cb)
    {
        _message_cb = cb;
    }

    void TcpConnection::set_high_water_cb(const HighWaterMarkCallback &cb, size_t mark)
    {
        _write_high_level_cb = cb;
        _high_level_mark = mark;
    }

    void TcpConnection::set_connection_cb(const ConnectingCallback &cb)
    {
        _connecting_cb = cb;
    }

    void TcpConnection::set_write_complete_cb(const WriteCompleteCallback &cb)
    {
        _write_complete_cb = cb;
    }

    void TcpConnection::set_close_cb(const CloseCallback &cb)
    {
        _close_cb = cb;
    }

    void TcpConnection::set_message_cb(MessageCallback &&cb) noexcept
    {
        _message_cb = std::move(cb);
    }

    void TcpConnection::set_high_water_cb(HighWaterMarkCallback &&cb, size_t mark) noexcept
    {
        _write_high_level_cb = std::move(cb);
        _high_level_mark = mark;
    }

    void TcpConnection::set_connection_cb(ConnectingCallback &&cb) noexcept
    {
        _connecting_cb = std::move(cb);
    }

    void TcpConnection::set_write_complete_cb(WriteCompleteCallback &&cb)noexcept
    {
        _write_complete_cb = std::move(cb);
    }

    void TcpConnection::set_close_cb(CloseCallback &&cb)noexcept
    {
        _close_cb = std::move(cb);
    }

    EventLoop *TcpConnection::get_loop() noexcept
    {
        return _loop;
    }

    uint64_t TcpConnection::get_id() const noexcept
    {
        return _id;
    }

    void TcpConnection::set_context(const Any &a)
    {
        _context = a;
    }

    void TcpConnection::set_context(Any &&a) noexcept
    {
        _context = std::move(a);
    }

    Any &TcpConnection::get_context()
    {
        return _context;
    }

    InetAddress TcpConnection::get_local_addr() const noexcept
    {
        return _local_addr;
    }

    InetAddress TcpConnection::get_peer_addr() const noexcept
    {
        return _peer_addr;
    }

    bool TcpConnection::is_connected() const noexcept
    {
        return _status == Connected;
    }

    int TcpConnection::get_fd() const noexcept
    {
        return _sockfd;
    }
}