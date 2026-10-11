// Objects: binding, dispatch in both directions, handles, script classes (spec §5, M5).
#include <doctest.h>
#include <moira/object.hpp>
#include <moira/parse.hpp>
#include <moira/script.hpp>
#include <moira/std_interop.hpp>

#include "alloc_counter.hpp"

#include <string>
#include <vector>
#include "ok.hpp"

using namespace moira;
using moira::test::ok;

namespace {

int g_mom_calls = 0;

// The talk's example.
struct Person : Object<Person> {
    float height = 6.0f;

    void tell_mom() { ++g_mom_calls; }
    float on_grow(float amount) {
        height += amount;
        tell_mom();
        return height;
    }

    static void bind(Binder<Person>& b) {
        b.method("grow", &Person::on_grow).doc("Grows by `amount`.");
        b.property("height", &Person::height);
        b.method("shrink", [](Person& self, float a) { self.height -= a; });
    }
};

struct Creature : Object<Creature> {
    int hp = 10;
    int damage(int n) { return hp -= n; }
    static void bind(Binder<Creature>& b) {
        b.method("damage", &Creature::damage);
        b.method("roar", [](Creature&) { return Symbol("grr"); });
        b.property("hp", &Creature::hp);
    }
};

struct Hero : Object<Hero, Creature> {
    int armor = 3;
    static void bind(Binder<Hero>& b) {
        b.inherits<Creature>();  // already implied by Object<Hero, Creature>
        b.method("roar", [](Hero&) { return Symbol("for glory"); });
        b.method("damage", [](Hero& h, int n) { return h.damage(n > h.armor ? n - h.armor : 0); });
        b.property("armor", &Hero::armor);
    }
};

}  // namespace

namespace {

Error error_of(Context& ctx, std::string_view text) {
    auto r = ctx.eval(text, "obj.dta");
    REQUIRE_FALSE(r);
    return r.error();
}

}  // namespace

TEST_CASE("Person::grow runs from script and from C++") {
    Context ctx;
    Person bob;
    g_mom_calls = 0;
    ok(ctx.add(bob, "bob"));
    CHECK(bob.type().name == "Person");
    CHECK(ok(ctx.eval("{bob grow 10}")).as_float() == 16.0);  // script -> C++
    CHECK(bob.height == 16.0f);
    CHECK(ok(ctx.call(bob, "grow", 1.5)).as_float() == 17.5);  // C++ -> the same dispatch
    CHECK(bob.on_grow(0.5f) == 18.0f);                         // and plain C++
    CHECK(g_mom_calls == 3);
    CHECK(ok(ctx.eval("{bob get height}")).as_float() == 18.0);
    CHECK(ok(ctx.eval("{bob set height 3}")).as_int() == 3);
    CHECK(bob.height == 3.0f);
    ok(ctx.eval("{bob shrink 1}"));
    CHECK(bob.height == 2.0f);
    CHECK(ok(ctx.eval("{bob type}")).as_sym() == Symbol("Person"));
    CHECK(ok(ctx.eval("{bob name}")).as_sym() == Symbol("bob"));
}

TEST_CASE("C++ calls a handler that script defined; script handlers come first") {
    Context ctx;
    Person bob;
    ok(ctx.add(bob, "bob"));
    ok(
        ctx.eval("{class Person\n"
                 "  (miss {set [height] {* [height] 2}} whoosh)\n"
                 "  (grow ($n) {set [height] {+ [height] {* $n 100}}})}"));
    CHECK(ok(ctx.call(bob, "miss")).as_sym() == Symbol("whoosh"));  // C++ -> script
    CHECK(bob.height == 12.0f);
    CHECK(ok(ctx.eval("{bob grow 1}")).as_float() == 112.0);  // the script handler wins
    CHECK_FALSE(ctx.var("this").is_set());                    // $this is restored afterwards
    CHECK(ok(ctx.eval("{bob shrink 2}")).as_int() == 0);      // the native method still serves
    CHECK(bob.height == 110.0f);
    // A handler's arity is checked.
    CHECK(error_of(ctx, "{bob grow}").message() == "'grow' takes 1 argument, given 0");
}

TEST_CASE("inheritance: methods and properties are flattened, a derived method replaces") {
    Context ctx;
    Hero hero;
    Creature rat;
    const Node h = ok(ctx.add(hero, "hero"));
    ok(ctx.add(rat, "rat"));
    CHECK(ok(ctx.eval("{rat roar}")).as_sym() == Symbol("grr"));
    CHECK(ok(ctx.eval("{hero roar}")).as_sym() == Symbol("for glory"));
    CHECK(ok(ctx.eval("{hero damage 5}")).as_int() == 8);  // the armor absorbs 3
    CHECK(ok(ctx.eval("{hero get hp}")).as_int() == 8);    // inherited property
    CHECK(ok(ctx.eval("{hero get armor}")).as_int() == 3);
    CHECK(ctx.try_get<Creature>(h) == &hero);
    CHECK(ctx.try_get<Hero>(h) == &hero);
    CHECK(ctx.try_get<Person>(h) == nullptr);
    CHECK(ctx.get<Creature>(h) == &hero);
    CHECK(to_string(ctx.get<Person>(h).error()) == "moira: the object is a Hero, not a Person");
    // Script handlers attached to a base type apply to derived objects.
    ok(ctx.eval("{class Creature (heal ($n) {set [hp] {+ [hp] $n}})}"));
    CHECK(ok(ctx.eval("{hero heal 2}")).as_int() == 10);
}

TEST_CASE("a destroyed object leaves a stale handle, never a dangling pointer") {
    Context ctx;
    Node old;
    {
        Person p;
        old = ok(ctx.add(p, "temp"));
        ctx.var("p") = old;
        CHECK(ok(ctx.eval("{$p grow 1}")).as_float() == 7.0);
    }  // p removes itself
    CHECK(ctx.try_resolve(old) == nullptr);
    CHECK(ctx.object("temp").type() == Type::Int);
    CHECK(error_of(ctx, "\n{$p grow 1}").message() ==
          "the object this command is sent to was removed (a stale handle)");
    CHECK(error_of(ctx, "{temp grow 1}").message() == "'temp' is not a function");
    Person q;
    const Node fresh = ok(ctx.add(q));  // likely reuses the slot, with a new salt
    CHECK(ctx.try_resolve(fresh) == &q);
    CHECK(ctx.try_resolve(old) == nullptr);
    CHECK(to_string(ctx.get<Person>(old).error()) == "moira: the Person object was removed");
}

TEST_CASE("object commands evaluate slot 0") {
    Context ctx;
    Person bob;
    ok(ctx.add(bob, "bob"));
    ok(ctx.func("nearest", [&](Context& c) { return c.object("bob"); }));
    CHECK(ok(ctx.eval("{{nearest} grow 1}")).as_float() == 7.0);
    ctx.var("who") = bob.node();
    CHECK(ok(ctx.eval("{$who grow 1}")).as_float() == 8.0);
    CHECK(error_of(ctx, "{{+ 1 2} grow 1}").message() ==
          "a command starts with a function or an object, not a int");
}

TEST_CASE("errors name the type, the method, and the call site") {
    Context ctx;
    Person bob;
    ok(ctx.add(bob, "bob"));
    Error e = error_of(ctx, "\n\n{bob fly}");
    CHECK(e.message() == "Person has no method 'fly'");
    CHECK(e.line() == 3);
    CHECK(error_of(ctx, "{bob grow tall}").message() ==
          "argument 1 of 'grow' is symbol, not float32");
    CHECK(error_of(ctx, "{bob set height tall}").message() ==
          "property 'height' of Person is float32, not symbol");
    CHECK(error_of(ctx, "{bob get weight}").message() ==
          "Person has no property or parameter 'weight'");
    CHECK(error_of(ctx, "{bob}").message() ==
          "an object command names a method: {object method ...}");
    CHECK_FALSE(ctx.add(bob, "bob2"));  // already in this Context
    Person other;
    CHECK(ctx.add(other, "bob").error().message() == "an object is already named 'bob'");
    CHECK(ctx.add(other, "if").error().message() ==
          "'if' is a function, so it cannot name an object");
}

TEST_CASE("loose parameters and property paths") {
    Context ctx;
    Person bob;
    ok(ctx.add(bob, "bob"));
    ok(ctx.eval("{bob set mood happy}"));
    CHECK(ok(ctx.eval("{bob get mood}")).as_sym() == Symbol("happy"));
    ok(ctx.eval("{bob set pos ((x 3) (y 4))}"));
    ok(ctx.eval("{class Person (where [pos x]) (feeling [mood])}"));
    CHECK(ok(ctx.eval("{bob where}")).as_int() == 3);
    CHECK(ok(ctx.eval("{bob feeling}")).as_sym() == Symbol("happy"));
    CHECK(error_of(ctx, "{+ [height] 1}").message() ==
          "[...] reads a property of $this, which is not set");
}

TEST_CASE("script classes: new, parameters, and delete") {
    Context ctx;
    ok(
        ctx.eval("{class Goblin\n"
                 "  (hit ($n) {set [hp] {- [hp] $n}})\n"
                 "  (alive {> [hp] 0})}\n"
                 "{new Goblin g1 (hp 5)}"));
    CHECK(ok(ctx.eval("{g1 hit 3}")).as_int() == 2);
    CHECK(ok(ctx.eval("{g1 alive}")).as_int() == 1);
    CHECK(ok(ctx.eval("{g1 hit 2} {g1 alive}")).as_int() == 0);
    CHECK(ok(ctx.eval("{g1 type}")).as_sym() == Symbol("Goblin"));
    CHECK(error_of(ctx, "{g1 fly}").message() == "Goblin has no method 'fly'");
    const Node g = ctx.object("g1");
    ok(ctx.eval("{delete g1}"));
    CHECK(ctx.try_resolve(g) == nullptr);
    CHECK(error_of(ctx, "{new Orc o}").message() == "'Orc' is not a class");
    // A default-constructible bound type can be created from script too.
    ctx.add_type<Person>();
    const Node p = ok(ctx.eval("{new Person tall (height 9)}"));
    REQUIRE(ctx.try_get<Person>(p));
    CHECK(ctx.try_get<Person>(p)->height == 9.0f);
    CHECK(ok(ctx.eval("{tall grow 1}")).as_float() == 10.0);
}  // the Context deletes `tall`, which it created

TEST_CASE("a Context destroyed before its objects leaves them unregistered") {
    Person bob;
    {
        Context ctx;
        ok(ctx.add(bob, "bob"));
        CHECK(bob.context() == &ctx);
    }
    CHECK(bob.context() == nullptr);
    CHECK(bob.node().type() == Type::Int);
    Context again;
    CHECK(again.add(bob, "bob"));  // can join another
}

TEST_CASE("calling a native method allocates nothing") {
    if (!test::counting_allocations()) return;
    Context ctx;
    Person bob;
    ok(ctx.add(bob, "bob"));
    auto tree = ok(read_string("{bob grow 0} {bob get height} {bob set height 6}"));
    for (int i = 0; i < 3; ++i) REQUIRE(ctx.run(tree->nodes()));
    test::AllocationScope scope;
    for (int i = 0; i < 100; ++i) REQUIRE(ctx.run(tree->nodes()));
    CHECK(scope.count() == 0);
}

TEST_CASE("check knows the methods of named objects and of objects the tree creates") {
    Context ctx;
    Person bob;
    ok(ctx.add(bob, "bob"));
    ctx.add_type<Person>();
    const auto tree =
        ok(read_string("{class Goblin (hit ($n) 0)}\n"
                       "{new Goblin g1}\n"
                       "{bob grow 1}\n"
                       "{bob grow}\n"
                       "{bob fly}\n"
                       "{g1 hit 1}\n"
                       "{g1 dance}\n"
                       "{bob get height}\n",
                       "level.dta"));
    const CheckReport report = check(*tree, ctx);
    REQUIRE(report.errors.size() == 1);
    CHECK(to_string(report.errors[0]) == "moira: 'grow' takes 1 argument, given 0  (level.dta:4)");
    REQUIRE(report.demand.size() == 2);
    CHECK(
        report.to_string().find("demand: 'fly' on Person is not defined, called at level.dta:5") !=
        std::string::npos);
    CHECK(report.to_string().find(
              "demand: 'dance' on Goblin is not defined, called at level.dta:7") !=
          std::string::npos);
}

TEST_CASE("the reference lists types, their methods and properties, and script classes") {
    Context ctx;
    ctx.add_type<Person>();
    ctx.add_type<Hero>();
    ok(ctx.eval("{class Person (miss whoosh)} {class Goblin (hit ($n) 0)}"));
    const std::string ref = reference(ctx);
    CHECK(ref.find("### `Person`\n\n"
                   "- `{<Person> grow float32}` → float32: Grows by `amount`.\n"
                   "- `{<Person> shrink float32}` → nothing\n"
                   "- property `height`: float32\n"
                   "- `{<Person> miss}` (script)\n") != std::string::npos);
    CHECK(ref.find("### `Hero`\n\nDerives from `Creature`.\n") != std::string::npos);
    CHECK(ref.find("### `Goblin` (script class)\n\n- `{<Goblin> hit $n}` (script)\n") !=
          std::string::npos);
}
