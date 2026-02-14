#pragma once

#include <array>
#include <string>

namespace tutti {

/// Default room names - Italian musical terms, A-P
struct RoomDef {
    const char* name;
    const char* meaning;
};

constexpr std::array<RoomDef, 16> kDefaultRooms = {{
    {"Allegro",     "lively, fast"},
    {"Ballata",     "a dance song"},
    {"Cantabile",   "in a singing style"},
    {"Dolce",       "sweetly"},
    {"Espressivo",  "expressively"},
    {"Fortepiano",  "loud then soft"},
    {"Giocoso",     "playfully"},
    {"Harmonics",   "overtone series"},
    {"Intermezzo",  "a short connecting piece"},
    {"Jubiloso",    "jubilantly"},
    {"Kaprizios",   "capricious, free-spirited"},
    {"Legato",      "smoothly connected"},
    {"Maestoso",    "majestically"},
    {"Notturno",    "a night piece"},
    {"Ostinato",    "a persistent pattern"},
    {"Pizzicato",   "plucked strings"},
}};

} // namespace tutti
