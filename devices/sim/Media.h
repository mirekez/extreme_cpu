#pragma once
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

// Host-side adapter only. Protocol matches mikOS's rootless Tribe peer/TAP bridge:
// HELLO = {1}; FRAME = {2, length_hi, length_lo, Ethernet bytes}.
class Media {
    int socket_ = -1;
    std::string local_;
    std::deque<std::vector<uint8_t>> receive_, transmit_;

public:
    explicit Media(const std::string& server) {
        if (server.empty()) {
            return;
        }
        local_ = server + "." + std::to_string(getpid());
        sockaddr_un source{}, target{};
        source.sun_family = target.sun_family = AF_UNIX;
        if (local_.size() >= sizeof(source.sun_path) || server.size() >= sizeof(target.sun_path)) {
            throw std::runtime_error("media socket path too long");
        }
        std::strcpy(source.sun_path, local_.c_str());
        std::strcpy(target.sun_path, server.c_str());
        socket_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (socket_ < 0) {
            throw std::runtime_error("cannot create media socket");
        }
        if (bind(socket_, reinterpret_cast<sockaddr*>(&source), sizeof(source))) {
            auto message = std::string(std::strerror(errno));
            close(socket_);
            throw std::runtime_error("cannot bind media socket: " + message);
        }
        if (connect(socket_, reinterpret_cast<sockaddr*>(&target), sizeof(target))) {
            auto message = std::string(std::strerror(errno));
            close(socket_);
            unlink(local_.c_str());
            throw std::runtime_error("cannot connect media socket: " + message);
        }
        const uint8_t hello = 1;
        if (send(socket_, &hello, 1, 0) != 1) {
            close(socket_);
            unlink(local_.c_str());
            throw std::runtime_error("cannot send media hello");
        }
    }

    ~Media() {
        if (socket_ >= 0) {
            close(socket_);
        }
        if (!local_.empty()) {
            unlink(local_.c_str());
        }
    }

    void poll() {
        if (socket_ < 0) {
            return;
        }
        while (!transmit_.empty()) {
            const auto& message = transmit_.front();
            auto result = send(socket_, message.data(), message.size(), 0);
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (result != static_cast<ssize_t>(message.size())) {
                throw std::runtime_error("Ethernet transmit failed");
            }
            transmit_.pop_front();
        }
        while (receive_.size() < 16) {
            uint8_t data[4096];
            auto count = recv(socket_, data, sizeof(data), 0);
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (count < 0) {
                throw std::runtime_error("Ethernet receive failed");
            }
            if (count < 3 || data[0] != 2) {
                continue;
            }
            unsigned size = (unsigned(data[1]) << 8) | data[2];
            if (size == 0 || size > 2048 || count != size + 3) {
                continue;
            }
            std::vector<uint8_t> packet(data + 3, data + 3 + size);
            // Peer retries during slow simulation must not exhaust the finite queue.
            bool duplicate = false;
            for (const auto& queued : receive_) {
                duplicate |= queued == packet;
            }
            if (!duplicate) {
                receive_.push_back(std::move(packet));
            }
        }
    }

    bool available() const {
        return !receive_.empty();
    }

    std::vector<uint8_t> take() {
        auto result = std::move(receive_.front());
        receive_.pop_front();
        return result;
    }

    bool ready() const {
        return socket_ >= 0 && transmit_.size() < 16;
    }

    bool drained() const {
        return transmit_.empty();
    }

    void send_frame(const std::vector<uint8_t>& bytes) {
        if (!ready()) {
            throw std::runtime_error("media queue full or no peer");
        }
        std::vector<uint8_t> message{2, uint8_t(bytes.size() >> 8), uint8_t(bytes.size())};
        message.insert(message.end(), bytes.begin(), bytes.end());
        transmit_.push_back(std::move(message));
    }
};
