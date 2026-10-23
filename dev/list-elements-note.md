# A List Describes Its Elements

A design note for requirement note 0025 of the editor integration, agreed with the owner on
Oct 23, 2026. Results and deviations go into `dev/decisions.md` when it is built.

## The need

A tweak or a parameter whose value is a list should say what each element is, so that a tool
that checks data without the game holds the data to it as the game does. D233 covers a list
of one-value elements (`std::vector<float>` is `values: ["float32"]`, `count: "any"`). Two
cases are still undescribed, and `values` is `null` for both:

- a variable-length list of the game's own type, `std::vector<vec2>`;
- a fixed-size list of one, `std::array<vec2, 4>`.

Each element of such a list nests in parentheses, `((0 0) (1 0) (1 1) (0 1))`, so its values
cannot be listed flat.

## The design

- **The converter says what its element is.** A list converter declares
  `using element = T;`, and a fixed-size one `static constexpr size_t length = N;` too.
  `Convert<std::vector<T>>` and `Convert<std::array<T, N>>` do. A game's own list converter
  may declare the same. It is read at compile time, at no cost to a game.
- **`baker::ListElement`** describes the element: its `type` by name (`"float32"`, `"vec2"`),
  its `values` and `count` as a value's are described, and the list's `length` (0: any
  number).
  - `TweakBase::list_element()` gives a tweak's.
  - A parameter's is in its `ParamText`, which a call never reads.
- **The JSON.**
  - Each tweak (`baker.tweaks/1.5`) and each parameter (`baker.reference/1.4`) gains
    `element`: `{"type", "values", "count"}`, or `null` for a value that is not a list.
  - It also gains `length`: N for a fixed-size list, `null` otherwise.
  - The element's `values` and `count` mean what a tweak's own do, and are `null` when the
    element's type does not say.
  - The tweak's own `values` and `count` stay as they are. These are new fields, so 1.x
    readers keep working.
- **Without the game.**
  - `TweakDescription` and `ParamDescription` take `.element` and its length.
  - `DescribedTweaks::add` makes a tweak that reads each element by its values: a
    one-value element bare, an element of several in its parentheses, as `Values::read`
    reads it. A fixed-size list needs exactly `length` elements.
  - `check`'s literal-argument check and a declared function's call hold a list argument
    the same way.
  - An element whose type says nothing (no values, not one of Baker's) is refused by
    `add`, naming the tweak, as a value with no values is today, so a tool leaves it out.
- **Range and `one_of` apply to each element.** `check_tweaks` holds every number in each
  element to the range, and every symbol or string to `one_of`, one level down.

## Cost

Compile-time only in the game. `ParamText` grows by the element's description; `ParamInfo`,
which a call reads, does not change. `TweakBase` gains one virtual that no read calls.

## Tests to write

- **JSON:** `std::vector<vec2>`, `std::array<vec2, 4>`, `std::vector<float>` and a
  non-list, each with its `element` and `length`.
- **Described tweaks:** an element of the wrong type, `(spawns 3 four 5)`; a point given
  one number; a fixed-size list with three points; each refused as the game refuses it, and
  good data accepted.
- **Range and `one_of`** over the elements of a list of points and of symbols.
- **Parameters:** a declared function with a `std::vector<vec2>` parameter refuses a
  literal argument with a bad point in `check`, and at a call.
- **Refusal:** an element type with no values is refused, naming the tweak.
