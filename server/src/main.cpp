#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <unistd.h>

#include "rooms/room_manager.h"
#include "signaling/http_server.h"
#include "signaling/ws_signaling.h"
#include "transport/rtc_transport.h"
#include "transport/session_binder.h"
#include "transport/wt_transport.h"

namespace {
std::atomic<int> g_signal_count{0};

void crash_handler(int sig) {
    void* frames[64];
    int n = backtrace(frames, 64);
    const char* msg = "\n=== CRASH (signal ";
    (void)write(STDERR_FILENO, msg, 18);
    // Write signal number
    char num[4]; int d = sig, pos = 0;
    if (d >= 10) num[pos++] = '0' + d/10;
    num[pos++] = '0' + d%10;
    num[pos++] = ')'; num[pos++] = '\n';
    (void)write(STDERR_FILENO, num, pos);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _exit(128 + sig);
}

void signal_handler(int) {
    int count = ++g_signal_count;
    if (count >= 2) {
        _exit(1);
    }
}
} // namespace

int main(int argc, char* argv[]) {
    // Parse command line
    std::string bind_address = "0.0.0.0";
    uint16_t http_port = 8080;
    uint16_t ws_port = 8081;
    uint16_t wt_port = 4433;
    size_t max_participants = 4;
    std::string cert_file = "certs/cert.pem";
    std::string key_file = "certs/key.pem";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--bind" && i + 1 < argc) {
            bind_address = argv[++i];
        } else if (arg == "--http-port" && i + 1 < argc) {
            http_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--ws-port" && i + 1 < argc) {
            ws_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--wt-port" && i + 1 < argc) {
            wt_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--max-participants" && i + 1 < argc) {
            max_participants = static_cast<size_t>(std::stoi(argv[++i]));
        } else if (arg == "--cert" && i + 1 < argc) {
            cert_file = argv[++i];
        } else if (arg == "--key" && i + 1 < argc) {
            key_file = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Tutti Server - Low-Latency Music Rehearsal\n\n"
                      << "Usage: tutti-server [options]\n\n"
                      << "Options:\n"
                      << "  --bind <addr>            Bind address (default: 0.0.0.0)\n"
                      << "  --http-port <port>       HTTP API port (default: 8080)\n"
                      << "  --ws-port <port>         WebSocket signaling port (default: 8081)\n"
                      << "  --wt-port <port>         WebTransport port (default: 4433)\n"
                      << "  --max-participants <n>   Max participants per room (default: 4)\n"
                      << "  --cert <path>            TLS certificate file (default: certs/cert.pem)\n"
                      << "  --key <path>             TLS private key file (default: certs/key.pem)\n"
                      << "  --help                   Show this help\n";
            return 0;
        }
    }

    std::cout << "╔══════════════════════════════════════╗\n"
              << "║       Tutti - All Together           ║\n"
              << "║   Low-Latency Music Rehearsal        ║\n"
              << "╚══════════════════════════════════════╝\n\n";

    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGSEGV, crash_handler);
    std::signal(SIGABRT, crash_handler);

    // Initialize room manager
    auto room_manager = std::make_shared<tutti::RoomManager>(max_participants);
    room_manager->initialize_default_rooms();
    std::cout << "[Tutti] Initialized 16 rooms\n";

    // Create session binder — routes transport events to rooms
    auto session_binder = std::make_shared<tutti::SessionBinder>(room_manager);
    auto binder_callbacks = session_binder->make_callbacks();

    // Start HTTP API server
    auto http_server = std::make_unique<tutti::HttpServer>(room_manager);

    // Load cert hash for WebTransport (from hash.txt alongside cert)
    {
        std::string hash_path = cert_file;
        auto last_slash = hash_path.rfind('/');
        if (last_slash != std::string::npos) {
            hash_path = hash_path.substr(0, last_slash + 1) + "hash.txt";
        } else {
            hash_path = "hash.txt";
        }
        std::ifstream hf(hash_path);
        std::string hash;
        if (hf && std::getline(hf, hash) && !hash.empty()) {
            http_server->set_cert_hash(hash);
            std::cout << "[Tutti] Cert hash loaded for WebTransport\n";
        }
    }

    if (!http_server->listen(bind_address, http_port)) {
        std::cerr << "[Tutti] Failed to start HTTP server\n";
        return 1;
    }

    // Start WebSocket signaling server (for WebRTC fallback)
    auto ws_signaling = std::make_unique<tutti::WsSignaling>();
    auto rtc_transport = std::make_shared<tutti::RtcTransportServer>();

    // Wire RTC transport callbacks through session binder
    rtc_transport->set_callbacks(binder_callbacks);

    // Wire signaling → RTC transport: when both DataChannels are ready,
    // create an RtcSession and fire it through the binder
    ws_signaling->set_on_session_ready(
        [rtc_transport, binder_callbacks](
            const std::string& session_id,
            std::shared_ptr<rtc::PeerConnection> pc,
            std::shared_ptr<rtc::DataChannel> audio_dc,
            std::shared_ptr<rtc::DataChannel> control_dc) {
            auto session = std::make_shared<tutti::RtcSession>(
                session_id, pc, audio_dc, control_dc);
            std::cout << "[Tutti] WebRTC session established: " << session_id << "\n";

            // Wire datagram and message callbacks for this session.
            // Capture session shared_ptr to prevent premature destruction.
            audio_dc->onMessage([binder_callbacks, session](auto data) {
                if (auto* binary = std::get_if<rtc::binary>(&data)) {
                    if (binder_callbacks.on_datagram) {
                        binder_callbacks.on_datagram(
                            session.get(),
                            reinterpret_cast<const uint8_t*>(binary->data()),
                            binary->size());
                    }
                }
            });

            control_dc->onMessage([binder_callbacks, session](auto data) {
                if (auto* text = std::get_if<std::string>(&data)) {
                    if (binder_callbacks.on_message) {
                        binder_callbacks.on_message(session.get(), *text);
                    }
                }
            });

            // Notify binder of new session
            if (binder_callbacks.on_session_open) {
                binder_callbacks.on_session_open(session);
            }

            // Handle disconnect
            pc->onStateChange([binder_callbacks, session](
                                  rtc::PeerConnection::State state) {
                if (state == rtc::PeerConnection::State::Disconnected ||
                    state == rtc::PeerConnection::State::Failed ||
                    state == rtc::PeerConnection::State::Closed) {
                    if (binder_callbacks.on_session_close) {
                        binder_callbacks.on_session_close(session.get());
                    }
                }
            });
        });

    if (!ws_signaling->listen(bind_address, ws_port)) {
        std::cerr << "[Tutti] Failed to start WebSocket signaling server\n";
        return 1;
    }

    // WebTransport server
    auto wt_transport = std::make_unique<tutti::WtTransportServer>();
    wt_transport->set_callbacks(binder_callbacks);
    wt_transport->set_cert_files(cert_file, key_file);
    wt_transport->listen(bind_address, wt_port);

    std::cout << "\n[Tutti] Server running. Press Ctrl+C to stop.\n"
              << "  HTTP API:     http://" << bind_address << ":" << http_port << "/api/rooms\n"
              << "  WS Signaling: ws://" << bind_address << ":" << ws_port << "\n"
              << "  WebTransport: https://" << bind_address << ":" << wt_port << "\n\n";

    // Main loop - wait for shutdown (Ctrl+C)
    while (g_signal_count == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n[Tutti] Shutting down...\n";
    http_server->stop();
    ws_signaling->stop();
    wt_transport->stop();

    std::cout << "[Tutti] Goodbye.\n";
    return 0;
}
