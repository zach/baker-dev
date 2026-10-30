# A Curve as a Value: `(curve ...)`

A design note, agreed with the owner on Oct 22, 2026. It answers a need from hosts whose
designers tune functions as well as numbers: reward by stage, difficulty by time, falloff by
distance. Survey input came from the Godot evaluation session: how curves are serialized
across engines, tools and interchange formats, and the RenderMan basis vocabulary. Results
and deviations go into `dev/decisions.md` when it is built.

## The need

Every engine has a curve type with an editor, and data that keeps a game's rules wants
curves beside its scalars. Today a host can write a `Convert` for its own curve over a list
of pairs. But `check`, the reference, shapes, panels and script know nothing of it.

## What the survey found

- **No single standard.** For a curve that is a setting rather than an animation, tools
  settle on a list of points with one interpolation mode for the whole curve: Lightroom's
  tone curve, GIMP's presets, Unreal's curve tables, CSS `linear()`.
- **Per-point tangents** have one portable convention: glTF's Hermite slopes. They convert
  losslessly to Unity, Unreal, Godot (which stores angles) and to unweighted Bezier handles.
  Weighted tangents and TCB do not survive conversion between tools.
- **The interpolation names** RenderMan, OSL, Houdini and USD share are `linear`,
  `catmull-rom`, `bezier`, `bspline`, `hermite`, plus Houdini's `constant` and
  `monotone`.
- **Catmull-Rom overshoots** between swinging values. Houdini added monotone cubic for
  ramps so a curve never dips below a key.

## The design

- **The value:** `(curve (0 1) (5 3) (10 8))`, points `(x y)` in order of x, read as one
  value.
  - **Interpolation:** an optional symbol after `curve` names it, for the whole curve:
    - `monotone`, the default: smooth, and never above or below its neighbouring keys;
    - `linear`;
    - `catmull-rom`;
    - `constant`, which holds each key's value to the next key.

    The names are RenderMan's and Houdini's, and the documentation says so.
  - **Per-point slopes:** a point may carry its in and out slopes as a third and fourth
    number, `(5 3 0.2 0.4)`, as Hermite slopes in glTF's convention. They replace the
    tangents its mode would compute at that point.
  - **Extrapolation:** `(extend clamp | linear | loop)` after the points says what lies
    beyond them. The default is clamp.
- **C++.** `baker::Curve` is a view over the points with `sample(x)`, and `Convert<Curve>`
  makes `get<Curve>` and `Tweak<Curve>` work. A host maps a `Curve` to its engine's type.
  Godot stores angles, which are atan of the slopes.
- **Script:** `{curve.sample path x}`.
- **Shapes:** a field type `curve`.
  - `check` holds a curve's points to ascending x, its mode to the four names, and its
    slopes and extension to their forms.
  - A field's `(range lo hi)` holds every y.
- **Layers replace a whole curve,** as for any list. A one-point tweak restates the curve.
- **Tools.** The reference and the tweaks JSON name the type `curve`, so a panel draws it
  as a curve. Write-back writes the points as they are.

## Cost

- Reading is the list it already is, with no allocation.
- `sample` is a binary search over the points and one segment's arithmetic. It is a script
  function and a C++ call, not a per-frame promise.

## Tests to write

- **Sampling each mode** at keys, between them, and beyond them under each extension.
- **Monotone** never leaves its neighbours' range on a curve that swings.
- **Slopes** given at a point are used.
- **`check` errors:** points out of order, an unknown mode, and a y outside a shape's range.
- **Tweaks and layers:** `Tweak<Curve>` reads and sets, and a layer replaces a curve whole.
- **Script:** `{curve.sample ...}`.
- **The JSON:** each document names the type `curve`.
