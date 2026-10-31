#pragma once

// baker::shape (spec §6, dev/decisions.md D84, D261): the expected structure of a data
// domain, declared in data, by whichever layer owns the vocabulary, with #shape:
//
//   #shape (boss_phase (@at bosses * phases)
//     (enter_at float (@range 0 1) (@kind percent) (@desc "HP fraction that starts it"))
//     (attacks  (list symbol))
//     (cadence  float (@range 0.2 5) (@optional)))
//
// A shape governs every array tagged with its name, or, given (@at ...), those whose
// ancestors' tags from the root are the ones listed (`*` matches any). A shape declared
// (@keyed) governs every child of the arrays at its (@at ...) path, whatever the child's tag:
// keyed data, as (fighters (kestrel (moves (jab ...) (kick ...)))), which layers merge by
// name (D108).
//
//   #shape (move (@at fighters * moves) (@keyed) (startup int (@range 1 60)))
//
// A field has a type (int, float, bool, symbol, string, any, curve, (list T), another shape's
// name, or (key_of TABLE)) and the tweak metadata, each with its sigil (spec §4, D248). A list
// may give its count: (list float 4) holds four values, (list float 1 3) one to three. A symbol
// field may list the symbols it allows, (button symbol (@one_of light heavy kick special)). A curve
// field's values are a curve (<baker/curve.hpp>), and its (@range lo hi) holds every point's y.
//
// A shape marked (@view) generates a C++ view, a struct read from its records
// (D269, D277): a field marked (@view no) stays out of the struct, an
// `any` field must be; (@cxx int64) picks the member's C++ type from a closed list, and
// (@cxx_name N) names the struct or the member when the shape's own name cannot.
//
// (key_of TABLE) holds a symbol to the keys of a keyed table: (weapon (key_of weapons)) in
// a unit names a child of (weapons (laser ...) (rifle ...)). TABLE is a dotted path, looked
// up by its first segment outward from the record that refers to it, the nearest enclosing
// table first and the root last, so (next (key_of moves)) in a fighter's move names that
// fighter's own move.
//
// check() reports a field of the wrong type, out of range or outside its list, a key its
// table lacks, or a required field that is missing, as an error; a field the shape does not
// declare is a warning, so adding data never requires touching its shape first. Commands
// and variables are not checked: their values are unknown until run.
//
//   cfg.add_validator(baker::shape::validator(), {.any_thread = true});  // at every merge
//
// Shapes are opt-in: unshaped data works everywhere. The library depends only on
// baker::data.

#include <baker/array.hpp>
#include <baker/config.hpp>
#include <baker/error.hpp>
#include <baker/list.hpp>
#include <baker/symbol.hpp>
#include <baker/text.hpp>
#include <baker/view.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace baker::shape {

enum class Type : uint8_t { Any, Int, Float, Bool, Symbol, String, List, Shape, Key, Curve };

struct Field {
    baker::Symbol name;
    Type type = Type::Any;
    Type element = Type::Any;   // a List's elements
    baker::Symbol shape;        // a Shape's, or a List of shapes', name
    List<baker::Symbol> table;  // a Key's, or a List of keys', table: a path, as written
    // How many values a List holds: (list T N) exactly N, (list T N M) N to M; any number
    // when not declared.
    uint32_t min_count = 0, max_count = UINT32_MAX;
    bool optional = false;
    bool ranged = false;
    double min = 0, max = 0;
    double step = 0;  // 0: none
    baker::Symbol kind;
    std::string_view desc, note;  // in the declaration, which the Set keeps
    // (@one_of light heavy kick): the symbols a symbol field's value may be, in the order
    // written; a field declared again adds to it. Empty: any symbol.
    List<baker::Symbol> one_of;
    // For a view (D277): whether the field is in the struct ((@view no) leaves it out), the
    // member's C++ type when (@cxx T) overrides the default (int8, int16, int32, int64,
    // uint8, uint16, uint32, uint64 for an int; float, double for a float), and the member's
    // name when (@cxx_name N) gives one. Empty: the defaults.
    bool view = true;
    baker::Symbol cxx;
    baker::Symbol cxx_name;
    // (@renamed_from old ...): the names the field had (D286). check() warns at a record
    // that spells one, the generated reader accepts it, and the kit's `format` rewrites it.
    List<baker::Symbol> renamed_from;
    baker::Symbol file;
    int32_t line = 0;
};

// (@rule "message" {expression} (@warn)): what must hold between a record's values. The
// shape library reads rules; baker::tools evaluates them (tools::check_rules), since this
// library cannot run script.
struct Rule {
    std::string_view message;  // in the declaration, which the Set keeps; names the rule
    Node expression;           // as written: $field reads the record's field
    bool warn = false;         // a failure is a warning, not an error
    baker::Symbol file;
    int32_t line = 0;
};

struct Shape {
    baker::Symbol name;
    List<baker::Symbol> at;  // ancestors' tags from the root; empty: anywhere
    bool keyed = false;      // governs every child at `at`, whatever its tag
    List<Field> fields;
    List<Rule> rules;
    bool view = false;       // (@view): generates a C++ view (D277)
    baker::Symbol cxx_name;  // (@cxx_name N): the struct's name; empty: from the shape's
    baker::Symbol file;
    int32_t line = 0;

    [[nodiscard]] const Field* field(baker::Symbol field_name) const noexcept;
    // The field `old` was renamed to, by its (@renamed_from ...); null for none (D286).
    [[nodiscard]] const Field* renamed(baker::Symbol old) const noexcept;
};

// A field check() found under a name its shape renamed (D286): what the kit's `format`
// rewrites, at the field's own array.
struct Renamed {
    baker::Symbol file;
    int32_t line = 0;
    baker::Symbol old;   // as the record spells it
    baker::Symbol name;  // the field's name now
};

struct Report {
    List<Error> errors;
    List<Error> warnings;  // fields a shape does not declare
    // Keys that resolved through a record's (@aka ...) rather than its key (D285): what a
    // tool lists as still in use, against aliases nothing names.
    List<baker::Symbol> aliases_used;
    // Fields spelled by a name their shape renamed (D286), in walk order; each is a warning
    // too, or an error when the record has the new name as well.
    List<Renamed> renames;

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

    // Reads every #shape the session's reads declared, or, given `root`, those of the read
    // that began with that file: one layer's (Config::layer_root(name)->file()). A field
    // declared again takes the new
    // type and whatever metadata the new declaration gives, as Config's merge of two layers
    // would. A malformed declaration is an error in `report`.
    //
    // Harder packs' declarations hold (D145): a field a softer pack declares
    // again may narrow a range or add metadata, but one that widens or drops a range, changes
    // the type, or makes a required field optional is a warning in `report`, and the harder
    // declaration stays. Files that declare no pack are unconstrained, as in the one-way rule.
    void add(const LoadSession& session, Report& report, baker::Symbol root = {});
    [[nodiscard]] std::span<const Shape> shapes() const noexcept;
    [[nodiscard]] const Shape* find(baker::Symbol name) const noexcept;

    struct State;

private:
    State* st_;
};

// --- Views (D269). ---

// The shapes a project's contract layers declare, as one Set: each layer's #shape forms,
// bottom to top, read through `session` and selected by the file each read began with
// (Set::add's `root`). A view's shape set is one per build, so a #shape inside an #ifdef
// or #ifndef branch of a contract layer's file, taken or skipped, is an error in `report`,
// located; so is a malformed declaration, as Set::add reports it.
[[nodiscard]] Set contract_set(const LoadSession& session, std::span<const baker::Symbol> roots,
                               Report& report);

// The digest a view generated from the shape asserts (kShapeDigest): symbol_hash's FNV-1a
// 64 (D246) over the shape's layout and nothing else, so tuning metadata never moves it.
// The text hashed, items joined by '\n': the shape's name; `keyed` if it is (@keyed);
// then, for each field in the struct (not (@view no)), in declared order, its name, its
// type spelled canonically (`int`, `list(float,3)`, `list(symbol,1,4)`, `key_of(a.b)`,
// `shape(NAME,DIGEST)` with the nested shape's own digest in 16 hex digits, or
// `list(shape(NAME,DIGEST))`), and its (@cxx T) if any. Zero for a shape the set lacks.
[[nodiscard]] uint64_t view_digest(const Set& shapes, baker::Symbol shape) noexcept;

// --- Generating views (D280). What baker_bake --views writes; a tool may call it too. ---

struct ViewOptions {
    std::string_view ns = "baker_views";  // the namespace the structs are generated in
};

// The shapes views are generated for: those marked (@view) and those `listed` (the project
// file's (views (shapes ...))), plus every shape they nest, sorted by name. Errors in
// `report`, located at the shape or field: a listed shape the set lacks, a nested shape it
// lacks, a field of type `any` not marked (@view no), a shape that nests itself, a generated
// name that is not a C++ identifier or is a keyword, two shapes whose struct names collide,
// a field whose member name collides with another's, and more than 64 required fields.
[[nodiscard]] List<baker::Symbol> view_shapes(const Set& shapes,
                                              std::span<const baker::Symbol> listed,
                                              Report& report);

// The generated text, deterministic for a set: the struct header of `shape` (its file is
// <shape>.hpp, reaching <baker/view.hpp>, <baker/curve.hpp> for a curve field, and the
// struct headers of the shapes it nests); its reader header (<shape>_read.hpp: the
// ViewReader<T> and Convert<T>, reaching <baker/view_read.hpp>); and the umbrella over
// `shapes` as view_shapes gives them (views.hpp: every struct header and kViews, a ViewInfo
// per view, for check_views). Call view_shapes first: these assume its checks passed.
void write_view_header(const Set& shapes, baker::Symbol shape, const ViewOptions& options,
                       TextSink out);
void write_view_reader(const Set& shapes, baker::Symbol shape, const ViewOptions& options,
                       TextSink out);
void write_views_umbrella(const Set& shapes, std::span<const baker::Symbol> views,
                          const ViewOptions& options, TextSink out);

// Drift (D282): the views a game compiled against the shapes its stack declares. Each
// compiled view (the generated kViews) is compared by name with the contract set's shape:
// a digest that differs, a shape the set lacks, or, for a nameless view (a shipping build
// keeps no names), a digest no shape's layout has, is a warning in `report`, located at
// the shape when there is one, saying to regenerate the views. A tool makes them errors.
void check_views(const Set& contract, std::span<const ViewInfo> compiled, Report& report);

// The reads a Config's contract layers began with, for contract_set: each named layer's
// own file, or, for a directory of layers, every file under it the stack read.
[[nodiscard]] List<baker::Symbol> contract_roots(const Config& cfg,
                                                 std::span<const baker::Symbol> contract_layers);

// A key a field names (D287): every value of a `key` field, or element of a list of keys,
// that check() reaches in a tree, with the table the field declares, as written.
struct KeyUse {
    baker::Symbol file;
    int32_t line = 0;
    baker::Symbol field;        // the field's tag
    List<baker::Symbol> table;  // the field's (key_of TABLE), a path from the root
    baker::Symbol key;          // the value as written
};
// Every key `tree` names, as check() finds the fields: what a tool renames with the record.
// The table need not be in `tree` (an overlay names a base's keys); nothing is checked.
[[nodiscard]] List<KeyUse> key_uses(const Array& tree, const Set& shapes);

// Checks every array of `tree` that a shape of `shapes` governs, into `report`. With
// `packs`, a key a harder pack's field names from a softer pack's table is a warning, as the
// one-way rule warns of a call (D80).
void check(const Array& tree, const Set& shapes, Report& report,
           const LoadSession* packs = nullptr);
// A warning in `report` for each shape of `shapes` that governs no array of `tree`: one whose
// (@at ...) names a place the data does not have (a keyed shape one level off checks
// nothing), or whose tag nothing carries. A tool's check, not a validator's: data a stack
// lacks for now is not an error at every merge.
void unused(const Array& tree, const Set& shapes, Report& report);
// Calls `visit` with each array of `tree` that a shape with rules governs (as check() finds
// them, by tag or at a keyed shape's path), the shape, and the array's path from the root:
// what tools::check_rules evaluates each rule over.
void each_ruled(
    const Array& tree, const Set& shapes,
    const Callback<void(const Array& record, const Shape& shape, std::string_view path)>& visit);
// For Config::add_validator: reads the shapes each layer of the stack declares, bottom to
// top, with the stack's session's packs (so harder packs' shapes hold, D145), and checks the
// merged root against them, reporting errors and warnings to the warning handler. It reads
// only its stack, so it may run on any thread:
//   cfg.add_validator(baker::shape::validator(), {.any_thread = true});
// It does not evaluate rules: add tools::rules_validator(ctx) for them. A build without
// baker::script warns once that the shapes hold rules it cannot check.
[[nodiscard]] Config::Validator validator();

// The shapes of `shapes` as JSON (schema baker.shapes/1.7, docs/reference-json.md), sorted by
// name: each shape's place, keyed or not, and its fields with their types, ranges, steps,
// kinds, documentation, and where they are declared, and its rules. For editors and agents.
void write_json(const Set& shapes, TextSink out);

}  // namespace baker::shape
