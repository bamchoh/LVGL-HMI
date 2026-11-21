// Transfers: values of one device copied to another through the tags (the HMI as a gateway).
//
//   "transfers": [
//     { "name": "speed", "from": "plc1_speed", "to": "plc2_speed" },                       // when the source changes
//     { "name": "temp",  "from": "plc1_temp",  "to": "plc2_temp", "trigger": "cyclic", "every_ms": 1000,
//       "scale": 0.1, "offset": 0, "when": { "tag": "run", "eq": 1 }, "out_of_range": "clamp", "enabled": true }
//   ]
//
// What is written is the value of the tag "from", times "scale" plus "offset", converted to the
// type of the tag "to". A value that is not a number, not finite or outside the limits of "to" is
// not written ("out_of_range": "clamp" writes the nearest limit instead).
//
// Nothing is written while the driver of either tag has no connection. After a connection comes
// back the transfer waits until the values were read again, then goes on with the newest value:
// what was read before the break is never written afterwards. Transfers that form a ring
// (a -> b -> a) are not run.
//
// The decisions are made in poll(now_ms), on the UI thread; tests hand in the clock and drivers
// that only say whether they are connected.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "jsonutil.hpp"

namespace hml {

class Driver;
class DriverRegistry;
class Tag;

constexpr size_t kTransferMax = 500;
constexpr uint32_t kTransferSettleMs = 2000;    // drivers that do not read in cycles: the wait after a reconnect

enum class TransferState { Ok, Disabled, Ring, NoTag, SourceOffline, SourceWait, DestOffline, DestWait, ConditionFalse, BadValue, OutOfRange };
const char* transfer_state_text(TransferState s);

struct TransferCond {
    enum class Op { None, Truthy, Eq, Ne, Gt, Lt, Ge, Le };
    Op op = Op::None;
    std::string tag, other_tag, text;       // compared with another tag, a text, or a number
    double number = 0;
    bool is_text = false;
};

struct TransferItem {
    std::string name, from, to;
    bool     cyclic = false;
    uint32_t every_ms = 1000;
    bool     enabled = true, clamp = false;
    double   scale = 1, offset = 0;
    TransferCond when;

    // ---- while it runs
    TransferState state = TransferState::Ok;
    uint64_t written = 0;                   // how often the destination got another value
    std::string from_driver, to_driver;     // the drivers the tags belong to ("" = none: a tag of the HMI)
};

// One entry of "transfers". False with a reason for what is none.
bool transfer_parse(const json& j, TransferItem& out, std::string* err = nullptr);
// The names of the transfers that are part of a ring.
std::vector<std::string> transfer_rings(const std::vector<TransferItem>& items);
// tag -> the driver it belongs to, from the "drivers" of a project (only drivers with a connection to lose)
std::vector<std::pair<std::string, std::string>> transfer_tag_drivers(const json* drivers);

class Transfers {
public:
    Transfers();
    ~Transfers();
    // Returns how many transfers are defined. The tags and the drivers must exist already.
    int  load(const json* section, const json* drivers_cfg, DriverRegistry& registry);
    void clear();
    void start();                           // the look ten times a second (after LVGL is up)
    size_t count() const;
    const std::vector<TransferItem>& items() const;
    // Decide and write. Returns how many values were written.
    int  poll(uint32_t now_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Transfers& transfers();

class SettingsPage;
std::unique_ptr<SettingsPage> make_transfers_page();    // System > Transfers: what runs, what waits and why

} // namespace hml
