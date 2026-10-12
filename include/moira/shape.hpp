#pragma once

// moira::shape (spec §6, docs/decisions.md D84): the expected structure of a data domain,
// declared in data, by whichever layer owns the vocabulary.
//
//   (shapes
//     (boss_phase (at bosses * phases)
//       (enter_at float (range 0 1) (kind percent) (desc "HP fraction that starts it"))
//       (attacks  (list symbol))
//       (cadence  float (range 0.2 5) (optional))))
//
// A shape governs every array tagged with its name, or, given (at ...), those whose
// ancestors' tags from the root are the ones listed (`*` matches any). A field has a type
// (int, float, bool, symbol, string, any, (list T), or another shape's name) and the tweak
// metadata of spec §4. check() reports a field of the wrong type or out of range, or a
// required field that is missing, as an error; a field the shape does not declare is a
// warning, so adding data never requires touching its shape first. Commands and variables
// are not checked: their values are unknown until run.
//
//   cfg.set_validator(moira::shape::validator());   // reported at every load and reload
//
// Shapes are opt-in: unshaped data works everywhere. The library depends only on
// moira::data.

#include <moira/array.hpp>
#include <moira/config.hpp>
#include <moira/error.hpp>
#include <moira/list.hpp>
#include <moira/symbol.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace moira::shape {

enum class Type : uint8_t { Any, Int, Float, Bool, Symbol, String, List, Shape };

struct Field {
    moira::Symbol name;
    Type type = Type::Any;
    Type element = Type::Any;  // a List's elements
    moira::Symbol shape;       // a Shape's, or a List of shapes', name
    bool optional = false;
    bool ranged = false;
    double min = 0, max = 0;
    double step = 0;  // 0: none
    moira::Symbol kind;
    std::string_view desc, note;  // in the declaration, which the Set keeps
    moira::Symbol file;
    int32_t line = 0;
};

struct Shape {
    moira::Symbol name;
    List<moira::Symbol> at;  // ancestors' tags from the root; empty: anywhere
    List<Field> fields;
    moira::Symbol file;
    int32_t line = 0;

    [[nodiscard]] const Field* field(moira::Symbol field_name) const noexcept;
};

struct Report {
    List<Error> errors;
    List<Error> warnings;  // fields a shape does not declare

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

// The shapes read from trees, for check() and for generated tuning panels (shapes()).
class Set {
public:
    Set();
    ~Set();
    Set(const Set& other);
    Set& operator=(const Set& other);
    Set(Set&& other) noexcept;
    Set& operator=(Set&& other) noexcept;

    // Reads every (shapes ...) at the top of `tree`. A field declared again takes the new
    // type and whatever metadata the new declaration gives, as Config's merge of two layers
    // would. A malformed declaration is an error in `report`.
    void add(const Array& tree, Report& report);
    [[nodiscard]] std::span<const Shape> shapes() const noexcept;
    [[nodiscard]] const Shape* find(moira::Symbol name) const noexcept;

    struct State;

private:
    State* st_;
};

// Checks every array of `tree` that a shape of `shapes` governs, into `report`.
void check(const Array& tree, const Set& shapes, Report& report);
// For Config::set_validator: reads the shapes the merged root declares and checks the root
// against them, reporting errors and warnings to the warning handler.
[[nodiscard]] Config::Validator validator();

}  // namespace moira::shape
