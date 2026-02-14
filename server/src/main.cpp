#include <csignal>
#include <iostream>
#include <memory>

#include "rooms/room_manager.h"
#include "signaling/http_server.h"
#include "signaling/ws_signaling.h"
#include "transport/rtc_transport.h"
#include "transport/wt_transport.h"

namespace {
std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}
} // namespace

int main(int argc, char* argv[]) {
    // Parse command line
    std::string bind_address = "0.0.0.0";
    uint16_t http_port = 8080;
    uint16_t ws_port = 8081;
    uint16_t wt_port = 4433;
    size_t max_participants = 4;

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
        } else if (arg == "--help") {
            std::cout << "Tutti Server - Low-Latency Music Rehearsal\n\n"
                      << "Usage: tutti-server [options]\n\n"
                      << "Options:\n"
                      << "  --bind <addr>            Bind address (default: 0.0.0.0)\n"
                      << "  --http-port <port>       HTTP API port (default: 8080)\n"
                      << "  --ws-port <port>         WebSocket signaling port (default: 8081)\n"
                      << "  --wt-port <port>         WebTransport port (default: 4433)\n"
                      << "  --max-participants <n>   Max participants per room (default: 4)\n"
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

    // Initialize room manager
    auto room_manager = std::make_shared<tutti::RoomManager>(max_participants);
    room_manager->initialize_default_rooms();
    std::cout << "[Tutti] Initialized 16 rooms\n";

    // Start HTTP API server
    auto http_server = std::make_unique<tutti::HttpServer>(room_manager);
    if (!http_server->listen(bind_address, http_port)) {
        std::cerr << "[Tutti] Failed to start HTTP server\n";
        return 1;
    }

    // Start WebSocket signaling server (for WebRTC fallback)
    auto ws_signaling = std::make_unique<tutti::WsSignaling>();
    auto rtc_transport = std::make_shared<tutti::RtcTransportServer>();

    // Wire up signaling → transport
    ws_signaling->set_on_session_ready(
        [room_manager, rtc_transport](
            const std::string& session_id,
            std::shared_ptr<rtc::PeerConnection> pc,
            std::shared_ptr<rtc::DataChannel> audio_dc,
            std::shared_ptr<rtc::DataChannel> control_dc) {
            auto session = std::make_shared<tutti::RtcSession>(
                session_id, pc, audio_dc, control_dc);
            std::cout << "[Tutti] WebRTC session established: " << session_id << "\n";
        });

    if (!ws_signaling->listen(bind_address, ws_port)) {
        std::cerr << "[Tutti] Failed to start WebSocket signaling server\n";
        return 1;
    }

    // WebTransport server (stub for now - requires libwtf integration)
    auto wt_transport = std::make_unique<tutti::WtTransportServer>();
    wt_transport->listen(bind_address, wt_port);

    std::cout << "\n[Tutti] Server running. Press Ctrl+C to stop.\n"
              << "  HTTP API:     http://" << bind_address << ":" << http_port << "/api/rooms\n"
              << "  WS Signaling: ws://" << bind_address << ":" << ws_port << "\n"
              << "  WebTransport: https://" << bind_address << ":" << wt_port << "\n\n";

    // Main loop - wait for shutdown
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n[Tutti] Shutting down...\n";
    http_server->stop();
    ws_signaling->stop();
    wt_transport->stop();

    std::cout << "[Tutti] Goodbye.\n";
    return 0;
}
