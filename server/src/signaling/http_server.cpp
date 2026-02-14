#include "http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace tutti {

HttpServer::HttpServer(std::shared_ptr<RoomManager> room_manager)
    : room_manager_(std::move(room_manager)) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::listen(const std::string& address, uint16_t port) {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[HTTP] Failed to create socket\n";
        return false;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &addr.sin_addr);

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[HTTP] Failed to bind to " << address << ":" << port << "\n";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, 16) < 0) {
        std::cerr << "[HTTP] Failed to listen\n";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread([this]() {
        while (running_) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd_,
                                   reinterpret_cast<sockaddr*>(&client_addr),
                                   &client_len);
            if (client_fd < 0) {
                if (running_) std::cerr << "[HTTP] Accept failed\n";
                continue;
            }
            // Handle in-line for simplicity (Phase 1)
            handle_connection(client_fd);
        }
    });

    std::cout << "[HTTP] Listening on " << address << ":" << port << "\n";
    return true;
}

void HttpServer::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void HttpServer::handle_connection(int client_fd) {
    char buf[4096];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buf[n] = '\0';

    // Parse request line
    HttpRequest req;
    std::istringstream stream(buf);
    stream >> req.method >> req.path;

    // Extract body (after double newline)
    const char* body_start = strstr(buf, "\r\n\r\n");
    if (body_start) {
        req.body = body_start + 4;
    }

    // Get remote IP
    sockaddr_in peer_addr{};
    socklen_t peer_len = sizeof(peer_addr);
    getpeername(client_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
    char ip_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_addr.sin_addr, ip_buf, sizeof(ip_buf));
    req.remote_ip = ip_buf;

    auto resp = route(req);

    // Build HTTP response
    std::ostringstream http_resp;
    http_resp << "HTTP/1.1 " << resp.status << " OK\r\n"
              << "Content-Type: " << resp.content_type << "\r\n"
              << "Content-Length: " << resp.body.size() << "\r\n"
              << "Access-Control-Allow-Origin: *\r\n"
              << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
              << "Access-Control-Allow-Headers: Content-Type\r\n"
              << "\r\n"
              << resp.body;

    std::string response = http_resp.str();
    send(client_fd, response.data(), response.size(), 0);
    close(client_fd);
}

HttpServer::HttpResponse HttpServer::route(const HttpRequest& req) {
    // CORS preflight
    if (req.method == "OPTIONS") {
        return {204, "text/plain", ""};
    }

    if (req.method == "GET" && req.path == "/api/rooms") {
        return handle_list_rooms();
    }

    // Extract room name from path: /api/rooms/:name/action
    if (req.path.substr(0, 11) == "/api/rooms/") {
        auto rest = req.path.substr(11);
        auto slash = rest.find('/');
        if (slash == std::string::npos) {
            return {404, "application/json", R"({"error":"not_found"})"};
        }
        std::string room_name = rest.substr(0, slash);
        std::string action = rest.substr(slash + 1);

        if (req.method == "POST" && action == "join") {
            return handle_join_room(room_name, req.body);
        }
        if (req.method == "POST" && action == "leave") {
            return handle_leave_room(room_name, req.body);
        }
        if (req.method == "POST" && action == "claim") {
            return handle_claim_room(room_name, req.body);
        }
        if (req.method == "POST" && action == "vacate-request") {
            return handle_vacate_request(room_name, req.remote_ip);
        }
    }

    return {404, "application/json", R"({"error":"not_found"})"};
}

HttpServer::HttpResponse HttpServer::handle_list_rooms() {
    auto rooms = room_manager_->list_rooms();
    nlohmann::json result = nlohmann::json::array();
    for (const auto& room : rooms) {
        result.push_back({
            {"name", room.name},
            {"participant_count", room.participant_count},
            {"max_participants", room.max_participants},
            {"claimed", room.claimed}
        });
    }
    return {200, "application/json", nlohmann::json{{"rooms", result}}.dump()};
}

HttpServer::HttpResponse HttpServer::handle_join_room(
    const std::string& room_name, const std::string& body) {
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(body);
    } catch (...) {
        return {400, "application/json", R"({"error":"invalid_json"})"};
    }

    std::string alias = req.value("alias", "Anonymous");
    std::string password = req.value("password", "");

    // Create a placeholder session for HTTP-only join
    // Real transport session will be established via WebTransport/WebRTC
    std::string participant_id;
    auto result = room_manager_->join_room(
        room_name, alias, password, nullptr, participant_id);

    switch (result) {
        case RoomManager::JoinResult::Success: {
            nlohmann::json resp = {
                {"participant_id", participant_id},
                {"wt_url", "https://localhost:4433/wt"},
                {"ws_url", "wss://localhost:4433/ws"}
            };
            return {200, "application/json", resp.dump()};
        }
        case RoomManager::JoinResult::RoomNotFound:
            return {404, "application/json", R"({"error":"room_not_found"})"};
        case RoomManager::JoinResult::RoomFull:
            return {409, "application/json", R"({"error":"room_full"})"};
        case RoomManager::JoinResult::PasswordRequired:
            return {401, "application/json", R"({"error":"password_required"})"};
        case RoomManager::JoinResult::PasswordIncorrect:
            return {401, "application/json", R"({"error":"password_incorrect"})"};
    }
    return {500, "application/json", R"({"error":"internal"})"};
}

HttpServer::HttpResponse HttpServer::handle_leave_room(
    const std::string& room_name, const std::string& body) {
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(body);
    } catch (...) {
        return {400, "application/json", R"({"error":"invalid_json"})"};
    }

    std::string participant_id = req.value("participant_id", "");
    if (participant_id.empty()) {
        return {400, "application/json", R"({"error":"missing_participant_id"})"};
    }

    room_manager_->leave_room(room_name, participant_id);
    return {200, "application/json", R"({"ok":true})"};
}

HttpServer::HttpResponse HttpServer::handle_claim_room(
    const std::string& room_name, const std::string& body) {
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(body);
    } catch (...) {
        return {400, "application/json", R"({"error":"invalid_json"})"};
    }

    std::string password = req.value("password", "");
    if (password.empty()) {
        return {400, "application/json", R"({"error":"missing_password"})"};
    }

    if (room_manager_->claim_room(room_name, password)) {
        return {200, "application/json", R"({"ok":true})"};
    }
    return {404, "application/json", R"({"error":"room_not_found"})"};
}

HttpServer::HttpResponse HttpServer::handle_vacate_request(
    const std::string& room_name, const std::string& remote_ip) {
    auto result = room_manager_->vacate_request(room_name, remote_ip);
    switch (result) {
        case RoomManager::VacateResult::Sent:
            return {200, "application/json", R"({"ok":true})"};
        case RoomManager::VacateResult::RoomNotFound:
            return {404, "application/json", R"({"error":"room_not_found"})"};
        case RoomManager::VacateResult::RoomEmpty:
            return {400, "application/json", R"({"error":"room_empty"})"};
        case RoomManager::VacateResult::CooldownActive:
            return {429, "application/json", R"({"error":"cooldown_active"})"};
    }
    return {500, "application/json", R"({"error":"internal"})"};
}

} // namespace tutti
