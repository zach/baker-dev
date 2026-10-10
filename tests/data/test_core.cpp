// Node, Symbol, Ref, String, Array: layout, ownership, and checked access.
#include <doctest.h>
#include <moira/array.hpp>
#include <moira/write.hpp>

#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace moira;

TEST_CASE("layout targets hold") {
    static_assert(sizeof(Node) == 16);
    static_assert(alignof(Node) == 8);
    static_assert(sizeof(Array) <= 24);
    CHECK(sizeof(Node) == 16);
    CHECK(sizeof(Array) == 24);
}

TEST_CASE("symbols are interned and compare by identity") {
    Symbol a("price");
    Symbol b(std::string("pri") + "ce");
    CHECK(a == b);
    CHECK(a.id() == b.id());
    CHECK(a.str() == "price");
    CHECK(a.hash() == detail::hash_symbol_text("price"));
    CHECK("price"_sym == a);
    CHECK_FALSE(Symbol("Price") == a);
    CHECK(Symbol() == Symbol(""));
    CHECK(Symbol().empty());
    CHECK(Symbol(std::string_view("with\0nul", 8)).str().size() == 8);
    CHECK(std::string(Symbol("abc").c_str()) == "abc");
}

TEST_CASE("symbol table grows and stays consistent") {
    std::vector<Symbol> syms;
    for (int i = 0; i < 20000; ++i) syms.emplace_back("grow_" + std::to_string(i));
    for (int i = 0; i < 20000; ++i)
        CHECK(syms[static_cast<size_t>(i)] == Symbol("grow_" + std::to_string(i)));
    CHECK(Symbol::table_size() >= 20000);
}

TEST_CASE("symbol interning is thread-safe") {
    std::vector<Symbol> a(2000), b(2000);
    std::thread t1([&] {
        for (size_t i = 0; i < a.size(); ++i) a[i] = Symbol("mt_" + std::to_string(i));
    });
    std::thread t2([&] {
        for (size_t i = 0; i < b.size(); ++i) b[i] = Symbol("mt_" + std::to_string(i));
    });
    t1.join();
    t2.join();
    CHECK(a == b);
}

TEST_CASE("node construction and types") {
    CHECK(Node().type() == Type::Int);
    CHECK(Node(7).type() == Type::Int);
    CHECK(Node(1.5f).type() == Type::Float);
    CHECK(Node(1.5).type() == Type::Float);
    CHECK(Node(Symbol("x")).type() == Type::Symbol);
    CHECK(Node(str("hi")).type() == Type::String);
    CHECK(Node(array(1, 2)).type() == Type::Array);
    CHECK(Node(array(1), Type::Command).type() == Type::Command);
    CHECK(Node(array(1), Type::Property).type() == Type::Property);
    CHECK(Node::var("v").type() == Type::Var);
    CHECK(Node::var("v").var_name() == Symbol("v"));
    CHECK(Node::object(ObjectHandle::make(3, 9)).object_handle().index() == 3);
    CHECK(Node::object(ObjectHandle::make(3, 9)).object_handle().salt() == 9);
    static const FuncRecord rec{"plus"};
    CHECK(Node::func(&rec).func_record() == &rec);
    CHECK(type_name(Type::Property) == "property");
}

TEST_CASE("numeric access coerces only where lossless") {
    CHECK(Node(3).try_float() == 3.0f);
    CHECK(Node(4.0f).try_int() == 4);
    CHECK_FALSE(Node(4.5f).try_int().has_value());
    CHECK_FALSE(Node(3e9f).try_int().has_value());
    CHECK_FALSE(Node(Symbol("x")).try_int().has_value());
    CHECK_FALSE(Node(Symbol("x")).try_float().has_value());
    CHECK(Node(Symbol("x")).try_sym() == Symbol("x"));
    CHECK_FALSE(Node(str("x")).try_sym().has_value());
    CHECK(Node(str("x")).try_str() == "x");
    CHECK_FALSE(Node(Symbol("x")).try_str().has_value());
    CHECK(Node(array(1)).try_array() != nullptr);
    CHECK(Node(array(1), Type::Command).try_array() == nullptr);
    CHECK(Node(array(1), Type::Command).array() != nullptr);
}

#if MOIRA_EXCEPTIONS
TEST_CASE("checked accessors throw located type errors") {
    CHECK(Node(12).as_int() == 12);
    CHECK(Node(12).as_float() == 12.0f);
    CHECK(Node(12).as<int64_t>() == 12);
    CHECK(Node(2.5f).as<double>() == 2.5);
    CHECK(Node(200).as<uint8_t>() == 200);
    CHECK_THROWS_AS((void)Node(300).as<uint8_t>(), Exception);
    CHECK_THROWS_AS((void)Node(-1).as<uint32_t>(), Exception);
    CHECK_THROWS_AS((void)Node(12.75f).as_int(), Exception);
    CHECK_THROWS_AS((void)Node(Symbol("a")).as_str(), Exception);
    CHECK_THROWS_AS((void)Node(str("a")).as_sym(), Exception);
    try {
        (void)Node(12.75f).as_int();
    } catch (const Exception& e) {
        CHECK(e.error().code == ErrorCode::Type);
        CHECK(e.error().message == "12.75 is float, not int");
    }
    auto a = Array::make(1, "where.dta", 12);
    try {
        (void)Node(a).as_int();
    } catch (const Exception& e) {
        CHECK(std::string(e.what()) == "moira: (0) is array, not int  (where.dta:12)");
    }
    CHECK_THROWS_AS((void)a->at(1), Exception);
    CHECK(a->at(0).as_int() == 0);
}
#endif

TEST_CASE("refcounts follow nodes and arrays") {
    Ref<Array> inner = array(1, 2);
    CHECK(inner->ref_count() == 1);
    {
        Node n(inner);
        CHECK(inner->ref_count() == 2);
        Node copy = n;
        CHECK(inner->ref_count() == 3);
        Node moved = std::move(copy);
        CHECK(inner->ref_count() == 3);
        CHECK(copy.type() == Type::Int);
        Ref<Array> outer = array("outer", n, moved);
        CHECK(inner->ref_count() == 5);
    }
    CHECK(inner->ref_count() == 1);

    Ref<String> s = str("shared");
    {
        Node a(s), b(s);
        CHECK(s->ref_count() == 3);
        a = b;
        CHECK(s->ref_count() == 3);
        a = Node(1);
        CHECK(s->ref_count() == 2);
    }
    CHECK(s->ref_count() == 1);
    CHECK(s->view() == "shared");
}

TEST_CASE("array construction helpers") {
    auto price = array("price", 12.75f);
    auto eggs = array("eggs", price);
    CHECK(to_string(*eggs) == "(eggs (price 12.75))");
    CHECK(eggs->height() == 2);
    CHECK(price->height() == 1);
    CHECK(array()->empty());
    CHECK(to_string(*array("name", str("Bob"), 3, -1.5, Node::var("x"))) ==
          "(name \"Bob\" 3 -1.5 $x)");
    auto made = Array::make(3, "f.dta", 9);
    CHECK(made->size() == 3);
    CHECK(made->file() == Symbol("f.dta"));
    CHECK(made->line() == 9);
    made->set(1, Node(array(array(1))));
    CHECK(made->height() == 3);
    CHECK(made->try_at(3) == nullptr);
    CHECK((*made)[1].array()->height() == 2);
}

TEST_CASE("structural equality ignores locations and aux bits") {
    auto a = Array::make_copy(std::vector<Node>{Node(1), Node(Symbol("x"))}, "a.dta", 1);
    auto b = Array::make_copy(std::vector<Node>{Node(1), Node(Symbol("x"))}, "b.dta", 99);
    CHECK(structurally_equal(*a, *b));
    Node n1(5), n2(5);
    n1.set_aux(0xABCDEF);
    CHECK(n1.aux() == 0xABCDEF);
    CHECK(structurally_equal(n1, n2));
    CHECK_FALSE(structurally_equal(Node(1), Node(1.0f)));
    CHECK_FALSE(structurally_equal(Node(0.0f), Node(-0.0f)));
    CHECK_FALSE(structurally_equal(Node(array(1)), Node(array(1), Type::Command)));
    CHECK(structurally_equal(Node(str("s")), Node(str("s"))));
    CHECK_FALSE(structurally_equal(Node(str("s")), Node(Symbol("s"))));
}

#if defined(MOIRA_ATOMIC_REFCOUNT) && MOIRA_ATOMIC_REFCOUNT
TEST_CASE("atomic refcounts survive concurrent copies") {
    Ref<Array> shared = array("shared", 1, 2, 3);
    auto work = [&] {
        for (int i = 0; i < 10000; ++i) {
            Node n(shared);
            Node m = n;
        }
    };
    std::thread t1(work), t2(work);
    t1.join();
    t2.join();
    CHECK(shared->ref_count() == 1);
}
#endif

TEST_CASE("Expected is std::expected when the library has it") {
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
    static_assert(MOIRA_HAS_STD_EXPECTED == 1);
    static_assert(std::is_same_v<Expected<int>, std::expected<int, Error>>);
#else
    static_assert(MOIRA_HAS_STD_EXPECTED == 0);
#endif
    MESSAGE("std::expected in use: " << MOIRA_HAS_STD_EXPECTED);
    Expected<int> ok = 3;
    Expected<int> bad = Unexpected(make_error(ErrorCode::NotFound, "nope"));
    CHECK(ok.has_value());
    CHECK(*ok == 3);
    CHECK_FALSE(bad.has_value());
    CHECK(bad.error().message == "nope");
    CHECK(bad.value_or(7) == 7);
    Expected<void> v;
    CHECK(v.has_value());
}
