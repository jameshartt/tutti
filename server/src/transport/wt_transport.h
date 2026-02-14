#pragma once

#include "transport_interface.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#ifdef TUTTI_WEBTRANSPORT
#include <wtf.h>
#endif

namespace tutti {

/// WebTransport session.
/// Provides unreliable datagrams for audio and bidirectional streams for control.
///
/// When TUTTI_WEBTRANSPORT is defined, uses libwtf + msquic.
/// Otherwise, acts as a stub (WebTransport disabled at build time).
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

#ifdef TUTTI_WEBTRANSPORT
    void set_wtf_session(wtf_session_t* s) { wt_session_ = s; }
    wtf_session_t* wtf_session() const { return wt_session_; }
    void set_control_stream(wtf_stream_t* s) { control_stream_ = s; }
    wtf_stream_t* control_stream() const { return control_stream_; }
#endif

private:
    std::string session_id_;
    std::string remote_addr_;
    std::atomic<bool> connected_{true};
#ifdef TUTTI_WEBTRANSPORT
    wtf_session_t* wt_session_ = nullptr;
    wtf_stream_t* control_stream_ = nullptr;
#endif
};

/// WebTransport server.
/// When TUTTI_WEBTRANSPORT is defined, listens via libwtf + msquic on QUIC/UDP.
/// Otherwise, logs a message and remains inactive.
class WtTransportServer : public TransportServer {
public:
    WtTransportServer();
    ~WtTransportServer() override;

    bool listen(const std::string& address, uint16_t port) override;
    void stop() override;
    void set_callbacks(TransportCallbacks callbacks) override;

    /// Set TLS certificate files (required for WebTransport)
    void set_cert_files(const std::string& cert_file, const std::string& key_file);

#ifdef TUTTI_WEBTRANSPORT
    /// Look up a WtSession by its libwtf session pointer
    std::shared_ptr<WtSession> find_session(wtf_session_t* wt_session);
#endif

private:
    TransportCallbacks callbacks_;
    std::atomic<bool> running_{false};
    std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<WtSession>> sessions_;
    std::string cert_file_;
    std::string key_file_;

#ifdef TUTTI_WEBTRANSPORT
    wtf_context_t* ctx_ = nullptr;
    wtf_server_t* server_ = nullptr;

    // libwtf callbacks (static, dispatch through user_context → this)
    static wtf_connection_decision_t on_connection(
        const wtf_connection_request_t* request, void* user_context);
    static void on_session_event(const wtf_session_event_t* event);
    static void on_stream_event(const wtf_stream_event_t* event);
#endif
};

} // namespace tutti
