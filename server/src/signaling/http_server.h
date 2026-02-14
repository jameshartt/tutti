#pragma once

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

#include "rooms/room_manager.h"

namespace tutti {

/// Simple HTTP server for room REST API.
/// Handles room listing, joining, leaving, claiming, and vacate requests.
///
/// Routes:
///   GET  /api/rooms              - List all rooms
///   POST /api/rooms/:name/join   - Join a room
///   POST /api/rooms/:name/leave  - Leave a room
///   POST /api/rooms/:name/claim  - Claim a room (set password)
///   POST /api/rooms/:name/vacate-request - Request occupants to vacate
class HttpServer {
public:
    explicit HttpServer(std::shared_ptr<RoomManager> room_manager);
    ~HttpServer();

    /// Set the TLS certificate hash (base64-encoded SHA-256) for WebTransport
    void set_cert_hash(const std::string& hash) { cert_hash_ = hash; }

    /// Start listening for HTTP connections
    bool listen(const std::string& address, uint16_t port);

    /// Stop the server
    void stop();

private:
    /// Handle an HTTP request (simple single-threaded for Phase 1)
    void handle_connection(int client_fd);

    /// Parse HTTP request and route to handler
    struct HttpRequest {
        std::string method;
        std::string path;
        std::string body;
        std::string remote_ip;
    };

    struct HttpResponse {
        int status = 200;
        std::string content_type = "application/json";
        std::string body;
    };

    HttpResponse route(const HttpRequest& req);
    HttpResponse handle_list_rooms();
    HttpResponse handle_join_room(const std::string& room_name,
                                  const std::string& body);
    HttpResponse handle_leave_room(const std::string& room_name,
                                   const std::string& body);
    HttpResponse handle_claim_room(const std::string& room_name,
                                   const std::string& body);
    HttpResponse handle_vacate_request(const std::string& room_name,
                                       const std::string& remote_ip);

    std::shared_ptr<RoomManager> room_manager_;
    std::string cert_hash_;
    int server_fd_ = -1;
    std::thread accept_thread_;
    std::atomic<bool> running_{false};
};

} // namespace tutti
