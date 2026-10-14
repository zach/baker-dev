// Hearth's households (game-stacks.md §4.3): characters with needs that decay, smart objects
// that advertise what they satisfy, and actions as script. No window here.
//
// A villager and a thing are Baker objects (Villager, Thing), so a designer changes one
// type's reaction in script, `{class villager (on_need_critical ($need) ...)}`, without
// touching C++. An action is a script handler on villager that starts a task on the clock,
// a Scheduler in game minutes:
//
//   {class villager
//      (eat_snack ($obj)
//         {clock thread_task {$this walk_to $obj} {wait {$this arrived}}
//            {$this use $obj} {wait {$this used}} {$this done}})}
//
// Scoring is native and runs only when a villager decides (§4.3, "push-up signals"): script
// runs when an action starts and while its task waits.
//
// A soak runs many households for many days on worker threads, each thread with its own
// Context and clock; a household's run depends only on its seed, so the results are the same
// on any number of threads.
#pragma once

#include <base/nav.hpp>
#include <base/rng.hpp>

#include <baker/config.hpp>
#include <baker/object.hpp>
#include <baker/task.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hearth {

enum class Curve : uint8_t { Linear, Quadratic, Logistic };

// A need, from (needs ...): 100 is met, 0 is desperate.
struct Need {
    std::string name;
    float decay_per_hour = 5;
    float critical = 20;  // below this, it may interrupt what the villager is doing
    Curve curve = Curve::Linear;
};

// A kind of smart object, from (objects ...).
struct Kind {
    std::string name;
    float size[3] = {1, 1, 1};  // metres: width, height, depth
    float color[3] = {0.7f, 0.7f, 0.7f};
    std::vector<std::pair<int, float>> advertise;  // need, points over one use
    baker::Symbol action;                          // the villager handler it starts
    float minutes = 30;                            // one use
    int users = 1;                                 // at once
    float hours[2] = {-1, -1};                     // when it is for, from and to; -1: any time
};

struct Resident {
    std::string name;
    std::vector<float> weights;  // per need; 1 by default
    float color[3] = {0.9f, 0.7f, 0.5f};
};

struct Placed {
    int kind = 0;
    float x = 0, z = 0, yaw = 0;  // yaw in degrees
};

// A household, from (households ...): its walls, things, and residents.
struct House {
    std::string name, desc;
    float size[2] = {14, 10};                 // metres, x and z; the floor is centered
    std::vector<std::array<float, 4>> walls;  // x z width depth, 2.5 m tall
    std::vector<Placed> things;
    std::vector<Resident> residents;
};

// (hearth ...): how villagers decide and what counts as a failure.
struct Rules {
    float walk_speed = 1.4f;       // m/s
    int choice_top = 3;            // picks among this many best
    float choice_within = 0.7f;    // ...that score at least this share of the best
    float distance_scale = 25;     // metres at which a thing's score halves
    float off_hours = 0.2f;        // a thing's score outside its hours, as a share
    float stuck_minutes = 60;      // no progress this long is stuck
    float starve_minutes = 120;    // a need at zero this long is starving
    int oscillation_switches = 6;  // interrupted back and forth this often is oscillating
    float idle_minutes = 10;       // when nothing scores, waits this long
};

struct Kit {
    std::vector<Need> needs;
    std::vector<Kind> kinds;
    std::vector<House> houses;
    Rules rules;
    std::vector<baker::Node> setup;  // the data's #autorun commands, for a worker's Context
    baker::Ref<baker::Array> tree;   // keeps `setup` alive

    [[nodiscard]] int need(const std::string& name) const;
    [[nodiscard]] int kind(const std::string& name) const;
    [[nodiscard]] int house(const std::string& name) const;
};

// Reads (hearth ...), (needs ...), (objects ...), and (households ...). False, with the
// reason, on a name that is not defined. The caller adds the setup commands (a Config's
// autorun()).
[[nodiscard]] bool load(const baker::Ref<baker::Array>& root, Kit& out, std::string& error);

class Household;

// A smart object in a household.
struct Thing : baker::Object<Thing> {
    Household* home = nullptr;
    int index = 0, kind = 0;
    float x = 0, z = 0, yaw = 0;
    float spot[3] = {0, 0, 0};  // where a user stands
    bool reachable = true;
    int users = 0;  // now
    int uses = 0;   // ever
    static void bind(baker::Binder<Thing>& b);
};

// What a villager's last decision weighed.
struct Decision {
    double minute = 0;
    struct Option {
        int thing = -1;  // -1: idle
        float score = 0;
    };
    std::vector<Option> options;  // best first
    int chosen = -1;              // index into options
    std::string why;              // "decided", "interrupted for hunger", ...
};

// The typed message a villager gets when a need falls below critical: script first, then
// the native default (interrupt unless the current action helps that need).
struct NeedCritical {
    static constexpr auto name = "on_need_critical";
    baker::Symbol need;
};

struct Villager : baker::Object<Villager> {
    enum class State : uint8_t { Idle, Deciding, Walking, Waiting, Using };

    Household* home = nullptr;
    int index = 0;
    std::string name;
    Resident resident;
    float x = 0, z = 0;
    std::vector<float> needs;         // per need, 0..100
    std::vector<float> zero_minutes;  // per need: how long it has been at zero
    std::vector<bool> below;          // per need: under critical already (one message each)
    State state = State::Idle;
    int thing = -1;        // what the current action is with; -1 idle
    baker::Symbol action;  // the current action's handler
    int64_t task = 0;      // its task on the clock; 0 none
    std::vector<base::NavPoint> path;
    size_t path_at = 0;
    bool walked = false;  // reached the end of the path
    float use_left = 0;   // minutes of using left
    double last_progress = 0;
    double idle_until = 0;
    std::vector<baker::Symbol> interrupted;  // kinds of thing cut short since one finished
    Decision last;
    std::vector<std::string> log;  // recent events, newest last

    // Script's view (bind): walk_to, arrived, use, used, done, interrupt, helps, need, say.
    bool walk_to(const baker::Node& obj);
    [[nodiscard]] bool arrived() const { return walked; }
    bool use(const baker::Node& obj);
    [[nodiscard]] bool used() const { return state == State::Using && use_left <= 0; }
    void done();
    void interrupt(baker::Symbol need);
    [[nodiscard]] bool helps(baker::Symbol need) const;
    [[nodiscard]] double need(baker::Symbol need) const;
    void say(const std::string& text);
    void on_need_critical(const NeedCritical& m);

    static void bind(baker::Binder<Villager>& b);
};

// The failures and the texture a run of a household measures (§4.3, "Metrics").
struct Tally {
    int characters = 0;
    double character_days = 0;
    int stuck = 0, oscillating = 0, starving = 0;
    int things = 0, things_used = 0;
    std::vector<int> uses_by_kind;  // per kind
    std::vector<double> entropy;    // per character: bits, the routine's mean over hours
    uint64_t steps = 0, decisions = 0, interrupts = 0, script_errors = 0;
    std::vector<std::string> notes;  // the first few failures, for a person

    void add(const Tally& o);
};

class Household {
public:
    // Builds house `house` of `kit` in `ctx`, whose objects named "clock" (a Scheduler in
    // game minutes) the actions use. The villagers' and things' names are unique to the
    // household (`prefix`). Seeded.
    Household(const Kit& kit, int house, baker::Context& ctx, baker::Scheduler& clock,
              uint64_t seed, const std::string& prefix = {});
    ~Household();
    Household(const Household&) = delete;
    Household& operator=(const Household&) = delete;

    // Advances the household by `seconds` of game time, then the clock, unless households
    // that share a clock step together and their owner ticks it once (`tick` false).
    void step(double seconds, bool tick = true);

    [[nodiscard]] double minute() const noexcept { return minute_; }
    [[nodiscard]] const House& house() const noexcept { return kit_.houses[size_t(house_)]; }
    [[nodiscard]] const Kit& kit() const noexcept { return kit_; }
    [[nodiscard]] std::vector<std::unique_ptr<Villager>>& villagers() noexcept {
        return villagers_;
    }
    [[nodiscard]] std::vector<std::unique_ptr<Thing>>& things() noexcept { return things_; }
    [[nodiscard]] const base::NavMesh& nav() const noexcept { return nav_; }
    [[nodiscard]] baker::Context& context() noexcept { return ctx_; }
    // The measures so far, with the routine entropy of the days run.
    [[nodiscard]] Tally tally() const;
    // A villager's last decision, as a person reads it.
    [[nodiscard]] std::string why(const Villager& v) const;
    [[nodiscard]] Villager* villager(const std::string& name);

    // For Villager and Thing.
    Thing* thing_of(const baker::Node& obj) const;
    void note(Villager& v, std::string text);
    void end_action(Villager& v, bool finished);
    void decide(Villager& v, const std::string& why);
    [[nodiscard]] double minute_now() const noexcept { return minute_; }
    [[nodiscard]] const Rules& rules() const noexcept { return kit_.rules; }

private:
    void build_nav();
    void walk(Villager& v, float seconds);
    void record_hour(Villager& v);
    float score(const Villager& v, const Thing& t) const;

    const Kit& kit_;
    int house_;
    baker::Context& ctx_;
    baker::Scheduler& clock_;
    baker::Node clock_node_;
    base::Rng rng_;
    double minute_ = 0;
    int last_hour_ = -1;
    base::NavMesh nav_;
    std::vector<std::unique_ptr<Thing>> things_;
    std::vector<std::unique_ptr<Villager>> villagers_;
    Tally tally_;
    std::vector<baker::Symbol> kind_symbols_;
    // Per villager, per hour of the day: how often it was about each kind of thing (or idle).
    std::vector<std::array<std::vector<std::pair<baker::Symbol, int>>, 24>> hours_;
};

// Makes a Context ready for households: the villager and thing types, and the clock. Then
// runs the kit's setup commands; their errors go to `errors`.
void prepare(baker::Context& ctx, baker::Scheduler& clock, const Kit& kit,
             std::vector<std::string>* errors = nullptr);

struct SoakOptions {
    int households = 50;
    double days = 30;
    uint64_t seed = 1;
    int threads = 0;           // 0: the machine's, at most 8
    double step_seconds = 30;  // game time per step
};
struct SoakResult {
    Tally tally;
    int households = 0, threads = 0;
    double wall_seconds = 0;  // not part of the result: it varies
    std::vector<std::string> errors;
};
// Runs `households` households (the kit's houses in turn) for `days` each, on worker
// threads, each with its own Context. Household i is seeded from (seed, i).
[[nodiscard]] SoakResult soak(const Kit& kit, const SoakOptions& options);

// The routine entropy band's ends and the share of characters inside it.
[[nodiscard]] double share_in_band(const Tally& t, double low, double high);

// Microseconds one 60 Hz step of `characters` villagers takes on this thread, at `speed`
// game seconds per real second, after a minute of warm-up. Reads the wall clock: for a
// person, not an objective.
[[nodiscard]] double bench(const Kit& kit, int characters, double speed);

}  // namespace hearth
