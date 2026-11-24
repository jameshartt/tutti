// Fuzzes SessionBinder::on_message, the entry point for every reliable
// message from a client, on both an unbound and a bound session, and
// on_game_datagram on the bound one. Nothing here may crash or throw.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "rooms/room_manager.h"
#include "transport/session_binder.h"

extern const char* const kFuzzSeeds[] = {
    "{\"type\":\"ping\",\"id\":1,\"t\":2.5}",
    "{\"type\":\"game_ping\",\"n\":7}",
    "{\"type\":\"game_unknown\\n[forged]\"}",
    "{\"type\":\"gain\",\"participant_id\":\"x\",\"value\":0.5}",
    "{\"type\":\"mute\",\"participant_id\":\"x\",\"muted\":true}",
    "{\"type\":\"codec\",\"codec\":\"opus\"}",
    "{\"type\":\"client_stats\",\"rtt\":1}",
    "{\"type\":\"bind\",\"participant_id\":\"p\",\"room\":\"Allegro\"}",
    "{\"type\":5}",
    "[1,2,3]",
    "{\"type\":\"gain\",\"participant_id\":5,\"value\":\"loud\"}",
};
extern const size_t kFuzzSeedCount = sizeof(kFuzzSeeds) / sizeof(kFuzzSeeds[0]);

namespace {

class NullSession : public tutti::TransportSession {
public:
    explicit NullSession(std::string id) : id_(std::move(id)) {}
    bool send_datagram(const uint8_t*, size_t) override { return true; }
    bool send_game_datagram(const uint8_t*, size_t) override { return true; }
    bool send_reliable(const std::string&) override { return true; }
    void close() override {}
    std::string id() const override { return id_; }
    std::string remote_address() const override { return "fuzz"; }
    bool is_connected() const override { return true; }
private:
    std::string id_;
};

struct Harness {
    std::shared_ptr<tutti::RoomManager> manager = std::make_shared<tutti::RoomManager>(4);
    std::unique_ptr<tutti::SessionBinder> binder;
    tutti::TransportCallbacks cb;
    std::shared_ptr<NullSession> bound = std::make_shared<NullSession>("bound");
    std::shared_ptr<NullSession> unbound = std::make_shared<NullSession>("unbound");

    Harness() {
        manager->initialize_default_rooms();
        binder = std::make_unique<tutti::SessionBinder>(manager);
        cb = binder->make_callbacks();
        std::string room = manager->list_rooms().front().name;
        std::string pid;
        manager->join_room(room, "fuzz", "", nullptr, pid);
        cb.on_session_open(bound);
        cb.on_message(bound.get(),
                      "{\"type\":\"bind\",\"participant_id\":\"" + pid +
                      "\",\"room\":\"" + room + "\"}");
        cb.on_session_open(unbound);
        // The server logs rejected input; silence it so fuzzing is not
        // dominated by log output (libFuzzer itself writes via C stdio).
        std::cout.rdbuf(nullptr);
        std::cerr.rdbuf(nullptr);
    }
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static Harness* h = new Harness(); // lives for the whole process
    std::string message(reinterpret_cast<const char*>(data), size);
    h->cb.on_message(h->bound.get(), message);
    h->cb.on_message(h->unbound.get(), message);
    if (h->cb.on_game_datagram) h->cb.on_game_datagram(h->bound.get(), data, size);
    return 0;
}
