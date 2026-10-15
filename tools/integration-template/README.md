# Baker for TARGET

An integration of [Baker](https://github.com/zach/baker) with TARGET.

<!-- Template from baker-dev's tools/integration-template/ (INTEGRATIONS.md). Replace
     TARGET, choose this repository's license to suit TARGET's terms, and delete this
     comment. -->

## License

This repository is licensed under LICENSE-OF-THIS-REPOSITORY, which TARGET's terms
require or allow. Baker itself is MIT-licensed and is used here as a dependency. Nothing
in this repository is part of Baker.

TARGET is a product of its owner, who has not endorsed this project.

## Building

Baker is fetched at a released version:

```cmake
include(FetchContent)
FetchContent_Declare(baker GIT_REPOSITORY https://github.com/zach/baker.git GIT_TAG vX.Y.Z)
FetchContent_MakeAvailable(baker)
```

## Requirements for Baker

When this integration needs something Baker lacks, it is written down in `requirements/`,
in Baker's own terms, from `requirements/0000-template.md`. Only the text of a requirement
goes to Baker; never code, identifiers or documentation from TARGET.
