# Five host needs from the Godot integration: a design note

*Requirement notes 0012 to 0016 of the Godot integration (D213 to D217). Each is built as
Baker's own, for any host with an embedded scripting language, files in an archive, or a
game started through an engine.*

## 0012: a function with a body and a described signature (D213)

`Context::declare(name, overloads, body)`: `declare` as today, with a body
(`Callback<Expected<Node>(Args&)>`) the host builds at runtime. `check` and the reference
see the described parameters, as for a declared function; a call runs the body.

- **At a call.** Arity is checked as for any overload. When no parameter is raw, each
  argument is evaluated once and held to its parameter's type, as a native's would be, and
  the body gets the evaluated arguments; a mismatch is the native's type error. With a raw
  parameter, the body gets the arguments unevaluated and evaluates what it needs, as a
  function taking `Args` does, and only `check` holds the literals.
- **Nothing on a native's path:** the body sits in the declared function's own callable.

## 0013: scenario directories through the host (D214)

`CommandHooks::list` (`Callback<bool(std::string_view dir, Callback<void(std::string_view
file)>)>`): the host lists a directory of its own file system. The kit keeps the `.dta`
names, sorts them, and reads each through the host's reader. Without it, directories are
read from the disk as today. A project can already name several files in one entry,
`(scenarios "sim/a.dta" "sim/b.dta")`.

## 0014: allowed values for string and integer tweaks (D215)

- **Strings** take `one_of` as symbols do: `TweakMeta::one_of`, or `(one_of "light" "heavy")`
  after the value in the data.
- **Integers** take `TweakMeta::choices`, each a value and a name (`{0, "easy"}`), or
  `(one_of (0 easy) (1 normal))` after the value; a bare number is a choice without a name.
- `check_tweaks` holds the value to the list, located, as for a symbol; the tweak still
  takes any value, as today.
- `baker.tweaks/1.3`: `one_of` lists a string tweak's strings as it lists symbols, and a new
  `choices` lists an integer tweak's `{"value", "name"}`s (`[]` for any other tweak).

## 0015: the live loop's console on a free port (D216)

`LiveOptions::console_any_port`: listen on a free port, which `console_port()` reports. 0
keeps meaning no console; a shipping build opens none.

## 0016: the tool entry with arguments (D217)

`(tool "path" "arg"...)`: `Project::tool` is the binary as today; `Project::tool_args`
keeps the arguments as written, in order. A tool runs them from the project file's
directory, so a relative path among them names the same file the binary's does.
