#pragma once

// What the generator will emit for these shapes (D275), written by
// hand so the runtime side is tested before the generator exists, and so the benchmarks
// read a view. Step 4's generator must produce exactly this for them:
//
//   #shape (vec2 (@view) (x float) (y float))
//   #shape (move (@at fighters * moves) (@keyed) (@view)
//      (startup     int (@range 1 60) (@cxx uint16))
//      (damage      float)
//      (name        string (@optional))
//      (button      symbol (@optional))
//      (armored     bool (@optional))
//      (cancel_into (list symbol) (@optional))
//      (hitbox      vec2 (@optional))
//      (points      (list vec2 1 4) (@optional))
//      (ramp        curve (@optional))
//      (on_hit      any (@view no)))

#include <baker/curve.hpp>
#include <baker/view.hpp>
#include <baker/view_read.hpp>

#include <cstdint>
#include <string_view>

namespace views_test {

struct Vec2 {
    float x = 0;
    float y = 0;
    static constexpr std::string_view kShapeName = baker::view_text("vec2");
    static constexpr uint64_t kShapeDigest = 0x1ull;  // step 4 generates the real one
};

struct Move {
    uint16_t startup = 0;
    float damage = 0;
    std::string_view name;
    baker::Symbol button;
    bool armored = false;
    baker::ListView<baker::Symbol> cancel_into;
    Vec2 hitbox;
    baker::ListView<Vec2> points;
    baker::Curve ramp;
    static constexpr std::string_view kShapeName = baker::view_text("move");
    static constexpr uint64_t kShapeDigest = 0x2ull;
};

// The five scalar fields alone, for the benchmark's like-for-like row against five finds:
//   #shape (scalars (@view) (startup int (@cxx uint16)) (damage float) (name string)
//      (button symbol) (armored bool))
struct Scalars {
    uint16_t startup = 0;
    float damage = 0;
    std::string_view name;
    baker::Symbol button;
    bool armored = false;
    static constexpr std::string_view kShapeName = baker::view_text("scalars");
    static constexpr uint64_t kShapeDigest = 0x3ull;
};

}  // namespace views_test

namespace baker {

template <>
struct ViewReader<views_test::Vec2> {
    static bool read(const Array& record, views_test::Vec2& out, Error* err) noexcept {
        using namespace baker::literals;
        uint32_t seen = 0;  // required fields, a bit each
        for (const Node& n : record.nodes().subspan(1)) {
            const Array* f = n.try_array();
            if (!f) continue;
            const Symbol tag = f->tag();
            // Dispatch on the name's hash (D246): no literal interns, and one string compare
            // on the match, so two names that share a hash cannot be confused.
            switch (tag.hash()) {
                case "x"_hash:
                    if (tag.str() != "x") break;
                    if (!view_detail::read_float(*f, out.x, err, "vec2", "float")) return false;
                    seen |= 1u;
                    break;
                case "y"_hash:
                    if (tag.str() != "y") break;
                    if (!view_detail::read_float(*f, out.y, err, "vec2", "float")) return false;
                    seen |= 2u;
                    break;
                default: break;
            }
        }
        if (seen != 3u) {
            if (err) *err = view_detail::missing(record, "vec2", (seen & 1u) ? "y" : "x");
            return false;
        }
        return true;
    }
};
template <>
struct Convert<views_test::Vec2> {
    static constexpr std::string_view name = "vec2";
    static bool from(const Values& v, views_test::Vec2& out) noexcept {
        return v.source() && ViewReader<views_test::Vec2>::read(*v.source(), out, nullptr);
    }
};

template <>
struct ViewReader<views_test::Move> {
    static bool read(const Array& record, views_test::Move& out, Error* err) noexcept {
        using namespace baker::literals;
        uint32_t seen = 0;  // required fields, a bit each
        for (const Node& n : record.nodes().subspan(1)) {
            const Array* f = n.try_array();
            if (!f) continue;
            const Symbol tag = f->tag();
            switch (tag.hash()) {
                case "startup"_hash:
                    if (tag.str() != "startup") break;
                    if (!view_detail::read_int(*f, out.startup, err, "move", "uint16"))
                        return false;
                    seen |= 1u;
                    break;
                case "damage"_hash:
                    if (tag.str() != "damage") break;
                    if (!view_detail::read_float(*f, out.damage, err, "move", "float"))
                        return false;
                    seen |= 2u;
                    break;
                case "name"_hash:
                    if (tag.str() != "name") break;
                    if (!view_detail::read_string(*f, out.name, err, "move")) return false;
                    break;
                case "button"_hash:
                    if (tag.str() != "button") break;
                    if (!view_detail::read_symbol(*f, out.button, err, "move")) return false;
                    break;
                case "armored"_hash:
                    if (tag.str() != "armored") break;
                    if (!view_detail::read_bool(*f, out.armored, err, "move")) return false;
                    break;
                case "cancel_into"_hash:
                    if (tag.str() != "cancel_into") break;
                    if (!view_detail::read_list(*f, out.cancel_into, err, "move", 0, UINT32_MAX))
                        return false;
                    break;
                case "hitbox"_hash:
                    if (tag.str() != "hitbox") break;
                    if (!view_detail::read_nested(*f, out.hitbox, err)) return false;
                    break;
                case "points"_hash:
                    if (tag.str() != "points") break;
                    if (!view_detail::read_list(*f, out.points, err, "move", 1, 4)) return false;
                    break;
                case "ramp"_hash:
                    if (tag.str() != "ramp") break;
                    if (!view_detail::read_curve(*f, out.ramp, err, "move")) return false;
                    break;
                default: break;
            }
        }
        if (seen != 3u) {
            if (err)
                *err = view_detail::missing(record, "move", (seen & 1u) ? "damage" : "startup");
            return false;
        }
        return true;
    }
};
template <>
struct ViewReader<views_test::Scalars> {
    static bool read(const Array& record, views_test::Scalars& out, Error* err) noexcept {
        using namespace baker::literals;
        uint32_t seen = 0;  // required fields, a bit each
        for (const Node& n : record.nodes().subspan(1)) {
            const Array* f = n.try_array();
            if (!f) continue;
            const Symbol tag = f->tag();
            switch (tag.hash()) {
                case "startup"_hash:
                    if (tag.str() != "startup") break;
                    if (!view_detail::read_int(*f, out.startup, err, "scalars", "uint16"))
                        return false;
                    seen |= 1u;
                    break;
                case "damage"_hash:
                    if (tag.str() != "damage") break;
                    if (!view_detail::read_float(*f, out.damage, err, "scalars", "float"))
                        return false;
                    seen |= 2u;
                    break;
                case "name"_hash:
                    if (tag.str() != "name") break;
                    if (!view_detail::read_string(*f, out.name, err, "scalars")) return false;
                    seen |= 4u;
                    break;
                case "button"_hash:
                    if (tag.str() != "button") break;
                    if (!view_detail::read_symbol(*f, out.button, err, "scalars")) return false;
                    seen |= 8u;
                    break;
                case "armored"_hash:
                    if (tag.str() != "armored") break;
                    if (!view_detail::read_bool(*f, out.armored, err, "scalars")) return false;
                    seen |= 16u;
                    break;
                default: break;
            }
        }
        if (seen != 31u) {
            if (err) {
                const char* const names[] = {"startup", "damage", "name", "button", "armored"};
                for (uint32_t i = 0; i < 5; ++i)
                    if (!(seen & (1u << i))) {
                        *err = view_detail::missing(record, "scalars", names[i]);
                        break;
                    }
            }
            return false;
        }
        return true;
    }
};

template <>
struct Convert<views_test::Move> {
    static constexpr std::string_view name = "move";
    static bool from(const Values& v, views_test::Move& out) noexcept {
        return v.source() && ViewReader<views_test::Move>::read(*v.source(), out, nullptr);
    }
};

}  // namespace baker
