# Declaring the game's types and objects from the reference: a design note

*Requirement note 0018, from the editor integration. Built as D211.*

## The need

A tool that checks data and script without the game (a language server, a CI checker, an
agent's pre-flight check) declares the game's functions from the reference
(`Context::declare`). It cannot yet declare the game's types or its named objects, so a
call on an object, `{scheduler delay_task 2 {...}}`, is not checked at all. Nor can it check
a parameter of a type of the game's own, such as a `vec2`: `{player.aim 1}` passes in the
tool and fails in the game.

## The shape

- **Value types of a parameter.** `ParamInfo` gains `values`, Baker's type of each value a
  parameter of a compound type reads (`Convert<T>::values`, as tweaks have since D210).
  `baker.reference/1.2` writes it on each parameter, `null` when the type does not say.
  `ParamDescription` gains the same field. `declare` takes a type it does not know when
  `values` describes it, and `check` holds a literal argument to those types, as the game's
  converter would read it with `Values::read`.
- **Types.** `Context::declare_type(name, bases, methods)` makes a type from the reference's
  description: its bases (types this Context already knows, native or declared), and each
  method with its overloads (`MethodDescription`: a name and its `OverloadDescription`s).
  The Context owns it. `check` then holds a call on an object of that type to its methods
  and its bases' methods, as it does for a native type.
- **Objects.** `Context::declare_object(name, type)` names an object of a known type for
  `check`: `{name method ...}` is checked against the type. Nothing is added at run time: a
  call on it fails as a call on an undefined name does.
- **Errors.** Each refuses with an error naming the type, method or object, and adds
  nothing: a base or a type it does not know, a parameter type it cannot check, a name that
  is already a function, an object or a type.

## Not changed

A native method's call path: `values` sits beside `match` in the parameter table, which
calls read only for literal checks and messages. Nothing runs for a game that does not
declare.
