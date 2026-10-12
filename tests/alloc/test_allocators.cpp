// D61: a Context and a Config allocate only through Moira allocators. This binary's main
// (main.cpp) installs a malloc default allocator before anything else runs, so global
// operator new can be watched on its own.

#include <doctest.h>

#include <moira/config.hpp>
#include <moira/object.hpp>
#include <moira/script.hpp>
#include <moira/task.hpp>
#include <moira/tweak.hpp>

#include "alloc_counter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace {

// Counts through malloc, never operator new.
struct Counting {
    size_t allocs = 0;
    size_t frees = 0;
    size_t live_bytes = 0;

    [[nodiscard]] moira::Allocator allocator() {
        return {[](void* u, size_t size, size_t) -> void* {
                    auto& c = *static_cast<Counting*>(u);
                    ++c.allocs;
                    c.live_bytes += size;
                    return std::malloc(size);
                },
                [](void* u, void* p, size_t size) {
                    auto& c = *static_cast<Counting*>(u);
                    ++c.frees;
                    c.live_bytes -= size;
                    std::free(p);
                },
                this};
    }
};

// Files the Config reads, by name.
const char* text_of(std::string_view path) {
    if (path == "base.dta")
        return "(window (width 640) (height 480)) (speed 2.5)\n#include extra.dta\n";
    if (path == "extra.dta") return "(title \"moira\") (list 1 2 3 4 5 6 7 8 9 10)";
    if (path == "over.dta") return "(speed 4.0) (window (width 800))";
    return nullptr;
}

bool read_text(std::string_view path, moira::TextSink out) {
    const char* t = text_of(path);
    if (!t) return false;
    out(t);
    return true;
}

bool discard(std::string_view, std::string_view) {
    return true;
}

struct Mover : moira::Object<Mover> {
    float x = 0;
    float step(float by) { return x += by; }
    static void bind(moira::Binder<Mover>& b) {
        b.method("step", &Mover::step);
        b.property("x", &Mover::x);
    }
};

// A file whose text changes with `g_version`, for reloads.
int g_version = 0;
bool read_versioned(std::string_view path, moira::TextSink out) {
    if (path != "live.dta") return false;
    char text[64];
    const int n =
        std::snprintf(text, sizeof text, "(speed %d) (list 1 2 3 \"s%d\")", g_version, g_version);
    out(std::string_view(text, static_cast<size_t>(n)));
    return true;
}

moira::Task counter(moira::Scheduler&, int* n) {
    for (int i = 0; i < 3; ++i) {
        ++*n;
        co_await moira::delay(1);
    }
}

int add(int a, int b) {
    return a + b;
}

}  // namespace

TEST_CASE("a Config allocates through its allocator, and nothing through operator new") {
    Counting mine;
    const moira::Allocator alloc = mine.allocator();
    size_t global = 0;
    bool ok = false;
    float speed_read = 0;
    {
        const moira::test::AllocationScope scope;  // no doctest assertion inside: they allocate
        {
            moira::TweakRegistry tweaks;
            moira::Tweak<float> speed{"speed", 1.0f, {}, tweaks};
            moira::Config config({.tweaks = &tweaks}, alloc);
            config.session().set_reader(&read_text);
            config.set_writer(&discard);
            ok = config.push_layer("base", "base.dta").has_value();
            ok = ok && config.push_layer("over", "over.dta").has_value();
            ok = ok && config.get<int>(moira::Path("window/width")).value_or(0) == 800;
            if constexpr (!moira::kShipping) {  // live edits are refused in shipping
                ok = ok && config.set("window/height", moira::Node(int64_t{600})).has_value();
                const moira::ConfigSnapshot snap = config.snapshot();
                ok = ok && config.reload().has_value();
                ok = ok && config.restore(snap).has_value();
                ok = ok && config.layer_effect("over").has_value();
            }
            ok = ok && !config.get<int>(moira::Path("missing")).has_value();  // an error record
            ok = ok && config.remove_layer("over").has_value();
            speed_read = speed;
        }
        global = scope.count();
    }
    CHECK(ok);
    CHECK(speed_read == 2.5f);
    if (moira::test::counting_allocations()) CHECK(global == 0);  // not under TSan
    CHECK(mine.allocs > 0);
    CHECK(mine.allocs == mine.frees);
    CHECK(mine.live_bytes == 0);
    MESSAGE("Config: " << mine.allocs << " allocations through its allocator");
}

TEST_CASE("a Context allocates through its allocator, and nothing through operator new") {
    Counting mine;
    const moira::Allocator alloc = mine.allocator();
    size_t global = 0;
    bool ok = false;
    {
        const moira::test::AllocationScope scope;
        {
            moira::Context ctx(alloc);
            ok = ctx.func("add", &add).has_value();
            ctx.add_type<Mover>();
            Mover bob;
            ok = ok && ctx.add(bob, "bob").has_value();
            ok = ok && ctx.eval("{func twice ($a) {add $a $a}} {set $n {twice 21}}").has_value();
            ok = ok && ctx.eval("{class Ball (bounce ($h) {add $h 1})} {new Ball b1}").has_value();
            ok = ok && ctx.eval("{b1 bounce 3}").value_or(moira::Node()).try_int() == 4;
            ok = ok && ctx.eval("{bob step 1.5} {new Mover m2 (x 2)} {delete m2}").has_value();
            ok = ok && !ctx.eval("{add 1}").has_value();  // an error record
        }
        global = scope.count();
    }
    CHECK(ok);
    if (moira::test::counting_allocations()) CHECK(global == 0);  // not under TSan
    CHECK(mine.allocs > 0);
    CHECK(mine.allocs == mine.frees);
    CHECK(mine.live_bytes == 0);
    MESSAGE("Context: " << mine.allocs << " allocations through its allocator");
}

TEST_CASE("reloads free the trees they replace") {
    if constexpr (moira::kShipping) return;  // reload is inert in shipping
    Counting mine;
    const moira::Allocator alloc = mine.allocator();
    size_t steady = 0;
    bool ok = true;
    {
        moira::Config config({.tweaks = nullptr}, alloc);
        config.session().set_reader(&read_versioned);
        ok = config.push_layer("live", "live.dta").has_value();
        for (int i = 1; i <= 50; ++i) {
            g_version = i;
            ok = ok && config.reload().value_or(false);
            if (i == 2) steady = mine.live_bytes;
        }
        ok = ok && config.get<int>(moira::Path("speed")).value_or(0) == 50;
        CHECK(mine.live_bytes == steady);  // each reload's arena replaced the last
    }
    CHECK(ok);
    CHECK(mine.live_bytes == 0);
}

TEST_CASE("tasks allocate through the Context's allocator, and nothing through operator new") {
    Counting mine;
    const moira::Allocator alloc = mine.allocator();
    size_t global = 0;
    bool ok = false;
    int n = 0;
    {
        const moira::test::AllocationScope scope;
        {
            moira::Context ctx(alloc);
            moira::Scheduler& s = ctx.scheduler();
            ok = ctx.func("add", &add).has_value();
            for (int i = 0; i < 100; ++i) ok = ok && s.spawn(counter(s, &n), "counter").has_value();
            ok = ok && ctx.eval(
                              "{do (($a 1)) {scheduler thread_task {add $a 1} {wait_tick} "
                              "{add $a 2}}} {scheduler interp_task $f 0 3 {add $f 1}} "
                              "{scheduler delay_task 2 {add 1 1}}")
                           .has_value();
            for (int i = 0; i < 6; ++i) s.tick(1);
            ok = ok && s.stats().live == 0;
            moira::Scheduler other(ctx, {.frame_bytes = 1024});
            ok = ok && other.spawn(counter(other, &n)).has_value();  // left running: freed with it
            other.tick(1);
        }
        global = scope.count();
    }
    CHECK(ok);
    CHECK(n == 301);
    if (moira::test::counting_allocations()) CHECK(global == 0);
    CHECK(mine.allocs == mine.frees);
    CHECK(mine.live_bytes == 0);
    MESSAGE("Context with tasks: " << mine.allocs << " allocations through its allocator");
}
