# Third-party code

Everything vendored into this repository, with its license. Only MIT, BSD, or
Apache-2.0 code may be added.

| Component | Version | License | Location | Used by |
|-----------|---------|---------|----------|---------|
| [doctest](https://github.com/doctest/doctest) | 2.4.11 | MIT (`third_party/doctest/LICENSE.txt`) | `third_party/doctest/` | Tests only |
| [fast_float](https://github.com/fastfloat/fast_float) | 8.3.2 | MIT, chosen from its MIT / Apache-2.0 / BSL-1.0 options (`third_party/fast_float/LICENSE-MIT`) | `third_party/fast_float/` | `moira::data` (private; parses floating-point literals) |
