#pragma once

#include "transport_interface.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace tutti {

/// WebTransport session via libwtf + msquic.
/// Provides unreliable datagrams for audio and bidirectional streams for control.
///
/// NOTE: libwtf integration is stubbed until the library proves stable.
/// The transport abstraction allows swapping to lsquic if needed.
class WtSession : public TransportSession {
public:
    explicit WtSession(const std::string& session_id,
                       const std::string& remote_addr);
    ~WtSession() override;

    bool send_datagram(const uint8_t* data, size_t len) override;
    bool send_reliable(const std::string& message) override;
    void close() override;
    std::string id() const override;
    std::string remote_address() const override;
    bool is_connected() const override;

private:
    std::string session_id_;
    std::string remote_addr_;
    std::atomic<bool> connected_{true};
    // TODO: libwtf session handle
    // wtf_session_t* wt_session_ = nullptr;
};

/// WebTransport server using libwtf + msquic
class WtTransportServer : public TransportServer {
public:
    WtTransportServer();
    ~WtTransportServer() override;

    bool listen(const std::string& address, uint16_t port) override;
    void stop() override;
    void set_callbacks(TransportCallbacks callbacks) override;

private:
    TransportCallbacks callbacks_;
    std::atomic<bool> running_{false};
    std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<WtSession>> sessions_;
    // TODO: libwtf/msquic server handle
};

} // namespace tutti
