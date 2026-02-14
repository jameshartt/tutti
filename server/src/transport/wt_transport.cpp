#include "wt_transport.h"

#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <unistd.h>

namespace tutti {

// ── AudioPacket serialization ───────────────────────────────────────────────

void AudioPacket::serialize(uint8_t* buf) const {
    // Little-endian uint32 writes
    std::memcpy(buf, &sequence, sizeof(uint32_t));
    std::memcpy(buf + 4, &timestamp, sizeof(uint32_t));
    std::memcpy(buf + kAudioHeaderSize, samples, kAudioPayloadSize);
}

AudioPacket AudioPacket::deserialize(const uint8_t* buf, size_t len) {
    AudioPacket pkt{};
    if (len < kAudioPacketSize) {
        // Short packet - zero-fill
        return pkt;
    }
    std::memcpy(&pkt.sequence, buf, sizeof(uint32_t));
    std::memcpy(&pkt.timestamp, buf + 4, sizeof(uint32_t));
    std::memcpy(pkt.samples, buf + kAudioHeaderSize, kAudioPayloadSize);
    return pkt;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

namespace {
std::string generate_session_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << "wt-" << std::hex << dist(gen);
    return oss.str();
}
} // namespace

// ── WtSession ───────────────────────────────────────────────────────────────

WtSession::WtSession(const std::string& session_id,
                     const std::string& remote_addr)
    : session_id_(session_id), remote_addr_(remote_addr) {}

WtSession::~WtSession() { close(); }

bool WtSession::send_datagram(const uint8_t* data, size_t len) {
    if (!connected_) return false;
#ifdef TUTTI_WEBTRANSPORT
    if (!wt_session_) return false;

    // Allocate buffer that libwtf will own — freed in DATAGRAM_SEND_STATE_CHANGE
    auto* buf_data = static_cast<uint8_t*>(malloc(len));
    if (!buf_data) return false;
    std::memcpy(buf_data, data, len);

    wtf_buffer_t buffer;
    buffer.length = static_cast<uint32_t>(len);
    buffer.data = buf_data;

    wtf_result_t result = wtf_session_send_datagram(wt_session_, &buffer, 1);
    if (result != WTF_SUCCESS) {
        free(buf_data);
        return false;
    }
    return true;
#else
    (void)data;
    (void)len;
    return false;
#endif
}

bool WtSession::send_reliable(const std::string& message) {
    if (!connected_) return false;
#ifdef TUTTI_WEBTRANSPORT
    if (!control_stream_) return false;

    // Newline-delimited framing (matching client's WebTransport implementation)
    std::string framed = message + "\n";

    // Allocate both data AND buffer struct on the heap.
    // wtf_stream_send() stores a direct pointer to the wtf_buffer_t (no copy),
    // so the struct must outlive the call. Both are freed in SEND_COMPLETE.
    auto* buf_data = static_cast<uint8_t*>(malloc(framed.size()));
    if (!buf_data) return false;
    std::memcpy(buf_data, framed.data(), framed.size());

    auto* buffer = static_cast<wtf_buffer_t*>(malloc(sizeof(wtf_buffer_t)));
    if (!buffer) {
        free(buf_data);
        return false;
    }
    buffer->length = static_cast<uint32_t>(framed.size());
    buffer->data = buf_data;

    wtf_result_t result = wtf_stream_send(control_stream_, buffer, 1, false);
    if (result != WTF_SUCCESS) {
        free(buf_data);
        free(buffer);
        return false;
    }
    return true;
#else
    (void)message;
    return false;
#endif
}

void WtSession::close() {
    bool was_connected = connected_.exchange(false);
    if (!was_connected) return;
#ifdef TUTTI_WEBTRANSPORT
    if (wt_session_) {
        wtf_session_close(wt_session_, 0, "session closed");
    }
    wt_session_ = nullptr;
    control_stream_ = nullptr;
#endif
}

std::string WtSession::id() const { return session_id_; }
std::string WtSession::remote_address() const { return remote_addr_; }
bool WtSession::is_connected() const { return connected_; }

// ── WtTransportServer ───────────────────────────────────────────────────────

WtTransportServer::WtTransportServer() = default;
WtTransportServer::~WtTransportServer() { stop(); }

void WtTransportServer::set_cert_files(const std::string& cert_file,
                                        const std::string& key_file) {
    cert_file_ = cert_file;
    key_file_ = key_file;
}

void WtTransportServer::set_callbacks(TransportCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

#ifdef TUTTI_WEBTRANSPORT

std::shared_ptr<WtSession> WtTransportServer::find_session(wtf_session_t* wt_session) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        if (session->wtf_session() == wt_session) {
            return session;
        }
    }
    return nullptr;
}

wtf_connection_decision_t WtTransportServer::on_connection(
    const wtf_connection_request_t* request, void* user_context) {
    (void)user_context;
    std::cout << "[WebTransport] Connection request from "
              << (request->authority ? request->authority : "unknown")
              << (request->path ? request->path : "/") << "\n";
    return WTF_CONNECTION_ACCEPT;
}

void WtTransportServer::on_session_event(const wtf_session_event_t* event) {
    // Retrieve the WtTransportServer instance from session user_context.
    // On CONNECTED, the user_context is the server (set via server config).
    // After CONNECTED, we set the session context to the WtSession.
    auto* server = static_cast<WtTransportServer*>(event->user_context);

    switch (event->type) {
        case WTF_SESSION_EVENT_CONNECTED: {
            std::string sid = generate_session_id();
            std::cout << "[WebTransport] Session connected: " << sid << "\n";

            auto session = std::make_shared<WtSession>(sid, "");
            session->set_wtf_session(event->session);

            // Store session and set the session's user_context to the server
            // so we can look up sessions in future callbacks
            {
                std::lock_guard<std::mutex> lock(server->sessions_mutex_);
                server->sessions_[sid] = session;
            }
            wtf_session_set_context(event->session, server);

            if (server->callbacks_.on_session_open) {
                server->callbacks_.on_session_open(session);
            }
            break;
        }

        case WTF_SESSION_EVENT_STREAM_OPENED: {
            auto session = server->find_session(event->session);
            if (!session) break;

            wtf_stream_type_t stype = event->stream_opened.stream_type;
            if (stype == WTF_STREAM_BIDIRECTIONAL) {
                // Client opened a bidirectional stream — use as control stream
                session->set_control_stream(event->stream_opened.stream);
                std::cout << "[WebTransport] Control stream opened for "
                          << session->id() << "\n";
            }

            // Set stream callback and context to the server for dispatching
            wtf_stream_set_callback(event->stream_opened.stream, on_stream_event);
            wtf_stream_set_context(event->stream_opened.stream, server);
            break;
        }

        case WTF_SESSION_EVENT_DATAGRAM_RECEIVED: {
            auto session = server->find_session(event->session);
            if (!session) break;

            if (server->callbacks_.on_datagram) {
                server->callbacks_.on_datagram(
                    session.get(),
                    event->datagram_received.data,
                    event->datagram_received.length);
            }
            break;
        }

        case WTF_SESSION_EVENT_DATAGRAM_SEND_STATE_CHANGE: {
            // Free send buffers when the send is finalized
            if (WTF_DATAGRAM_SEND_STATE_IS_FINAL(
                    event->datagram_send_state_changed.state)) {
                for (uint32_t i = 0;
                     i < event->datagram_send_state_changed.buffer_count; i++) {
                    free(event->datagram_send_state_changed.buffers[i].data);
                }
            }
            break;
        }

        case WTF_SESSION_EVENT_DISCONNECTED: {
            auto session = server->find_session(event->session);
            if (!session) break;

            std::cout << "[WebTransport] Session disconnected: "
                      << session->id() << " (error: "
                      << event->disconnected.error_code << ")\n";

            if (server->callbacks_.on_session_close) {
                server->callbacks_.on_session_close(session.get());
            }

            {
                std::lock_guard<std::mutex> lock(server->sessions_mutex_);
                server->sessions_.erase(session->id());
            }
            break;
        }

        case WTF_SESSION_EVENT_DRAINING:
            break;

        default:
            break;
    }
}

void WtTransportServer::on_stream_event(const wtf_stream_event_t* event) {
    auto* server = static_cast<WtTransportServer*>(event->user_context);
    if (!server) return;

    switch (event->type) {
        case WTF_STREAM_EVENT_DATA_RECEIVED: {
            // Reconstruct the message from buffers
            std::string data;
            for (uint32_t i = 0; i < event->data_received.buffer_count; i++) {
                data.append(
                    reinterpret_cast<const char*>(
                        event->data_received.buffers[i].data),
                    event->data_received.buffers[i].length);
            }

            // Find which session owns this stream
            // The stream is the control stream for one of our sessions
            std::shared_ptr<WtSession> session;
            {
                std::lock_guard<std::mutex> lock(server->sessions_mutex_);
                for (auto& [id, s] : server->sessions_) {
                    if (s->control_stream() == event->stream) {
                        session = s;
                        break;
                    }
                }
            }

            if (session && server->callbacks_.on_message) {
                // Split on newlines (our message delimiter)
                // Buffer partial messages per-session if needed
                std::istringstream iss(data);
                std::string line;
                while (std::getline(iss, line)) {
                    if (!line.empty()) {
                        server->callbacks_.on_message(session.get(), line);
                    }
                }
            }
            break;
        }

        case WTF_STREAM_EVENT_SEND_COMPLETE: {
            // Free the data payloads we malloc'd in send_reliable().
            // Do NOT free the wtf_buffer_t struct — libwtf frees it internally.
            for (uint32_t i = 0; i < event->send_complete.buffer_count; i++) {
                if (event->send_complete.buffers[i].data) {
                    free(event->send_complete.buffers[i].data);
                }
            }
            break;
        }

        case WTF_STREAM_EVENT_PEER_CLOSED:
        case WTF_STREAM_EVENT_CLOSED:
        case WTF_STREAM_EVENT_ABORTED:
            break;

        default:
            break;
    }
}

#endif // TUTTI_WEBTRANSPORT

bool WtTransportServer::listen(const std::string& address, uint16_t port) {
#ifdef TUTTI_WEBTRANSPORT
    if (cert_file_.empty() || key_file_.empty()) {
        std::cerr << "[WebTransport] TLS cert/key files not set. "
                  << "Run: cd server/certs && ./generate.sh\n";
        return false;
    }

    // Create context with low-latency profile
    wtf_context_config_t ctx_config{};
    ctx_config.log_level = WTF_LOG_LEVEL_TRACE;
    ctx_config.execution_profile = WTF_EXECUTION_PROFILE_LOW_LATENCY;
    ctx_config.worker_thread_count = 2;
    ctx_config.log_callback = [](wtf_log_level_t level, const char* component,
                                  const char* /*file*/, int /*line*/,
                                  const char* message, void* /*ctx*/) {
        std::cerr << "[libwtf:" << (component ? component : "?") << ":"
                  << level << "] " << message << "\n";
    };

    wtf_result_t status = wtf_context_create(&ctx_config, &ctx_);
    if (status != WTF_SUCCESS) {
        std::cerr << "[WebTransport] Failed to create context: "
                  << wtf_result_to_string(status) << "\n";
        return false;
    }

    // Resolve cert paths to absolute paths
    auto resolve_path = [](const std::string& path) -> std::string {
        if (path.empty() || path[0] == '/') return path;
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) {
            return std::string(cwd) + "/" + path;
        }
        return path;
    };
    std::string abs_cert = resolve_path(cert_file_);
    std::string abs_key = resolve_path(key_file_);

    std::cout << "[WebTransport] Using cert: " << abs_cert << "\n";
    std::cout << "[WebTransport] Using key:  " << abs_key << "\n";

    // Configure TLS certificate
    wtf_certificate_config_t cert_config{};
    cert_config.cert_type = WTF_CERT_TYPE_FILE;
    cert_config.cert_data.file.cert_path = abs_cert.c_str();
    cert_config.cert_data.file.key_path = abs_key.c_str();

    // Configure server (host=nullptr binds to all interfaces, matching echo_server)
    wtf_server_config_t srv_config{};
    srv_config.host = nullptr;
    srv_config.port = port;
    srv_config.cert_config = &cert_config;
    srv_config.session_callback = on_session_event;
    srv_config.connection_validator = on_connection;
    srv_config.user_context = this;
    srv_config.max_sessions_per_connection = 32;
    srv_config.max_streams_per_session = 256;
    srv_config.idle_timeout_ms = 60000;
    srv_config.handshake_timeout_ms = 10000;
    srv_config.enable_0rtt = true;
    srv_config.enable_migration = true;

    status = wtf_server_create(ctx_, &srv_config, &server_);
    if (status != WTF_SUCCESS) {
        std::cerr << "[WebTransport] Failed to create server: "
                  << wtf_result_to_string(status) << "\n";
        wtf_context_destroy(ctx_);
        ctx_ = nullptr;
        return false;
    }

    status = wtf_server_start(server_);
    if (status != WTF_SUCCESS) {
        std::cerr << "[WebTransport] Failed to start server: "
                  << wtf_result_to_string(status) << "\n";
        wtf_server_destroy(server_);
        wtf_context_destroy(ctx_);
        server_ = nullptr;
        ctx_ = nullptr;
        return false;
    }

    std::cout << "[WebTransport] Server listening on "
              << address << ":" << port << " (libwtf "
              << wtf_get_version()->version << ")\n";
    running_ = true;
    return true;

#else
    std::cout << "[WebTransport] Disabled at build time "
              << "(build with -DTUTTI_ENABLE_WEBTRANSPORT=ON to enable)\n";
    std::cout << "[WebTransport] Stub active on " << address << ":" << port << "\n";
    (void)address;
    (void)port;
    running_ = true;
    return true;
#endif
}

void WtTransportServer::stop() {
    if (!running_) return;
    running_ = false;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& [id, session] : sessions_) {
            session->close();
        }
        sessions_.clear();
    }

#ifdef TUTTI_WEBTRANSPORT
    if (server_) {
        wtf_server_stop(server_);
        wtf_server_destroy(server_);
        server_ = nullptr;
    }
    if (ctx_) {
        wtf_context_destroy(ctx_);
        ctx_ = nullptr;
    }
#endif
}

} // namespace tutti
