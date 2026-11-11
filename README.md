# Doxydown

Doxydown is a documentation generator for C++.
It is inspired to [doxygen](https://www.doxygen.nl) and is a fork of [Doxide](https://github.com/lawmurray/doxide).
Its main features are:

- you write documentation in pure, familiar **Markdown**: no more HTML tags for links, italics and so on;
- you can use familiar documentation **extensions inspired to doxygen**;
- it emits Markdown for several common tools (plain Markdown, MkDocs or PanDoc), which you can then use fo customization, post-processing and integration with other documentation.
- it is configured in familiar YAML
- it is open source under an Apache 2.0 license, and runs on Linux, Mac, and Windows.

## Why an alternative to doxygen

Doxygen is a tool that [dates back to 1997](https://en.wikipedia.org/wiki/Doxygen), when a lot of formats and tools that are now available did not exist.
Therefore, it adopted a format based on a mix of custom extensions and HTML (and some Latex, e.g., for mathematics), the main markup technology of those times.
In addition, it provides a full HTML generation, which enforces a certain look and features set (only recently updated) and has limited customization (one has to temper with CSS to change the look).

Modern tools allow lifting both limitations.
Markdown is a well-known, easy and extensible markup language that most users are familiar with, more than HTML, and can indeed replace it for the most common formatting options.
There is a multitude of tools ([MkDocs](https://www.mkdocs.org/) and [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/), PanDoc, MyST, ...) that extend it to provide additional features and that post-process it with much higher degrees of customizability, finally integrating it into your organization's documentation in a coherent way, as if all of the parts of your documentation had been generated in a single process.
The days when going from the tutorial to the API documentation seemed like a jump back to the 90's are finally over.

## Why an alternative to Doxide

Doxydown inherits a lot of design decisions from [Doxide](https://github.com/lawmurray/doxide), but focuses on modularity, extensibility and adherence to modern C++ practices like avoiding pointers and using robust C++ facilities like `unique_ptr`, `span`, `option` and so on.
This should make it easy to develop and extend Doxydown.

## License

Doxydown is open source software. It is licensed under the Apache License,
Version 2.0 (the "License"); you may not use it except in compliance with the
License. You may obtain a copy of the License at
<http://www.apache.org/licenses/LICENSE-2.0>.


## Requirements

The basic tools for building are

* A C++20 toolchain: full C++20 support is required (e.g., some compiler versions like GCC 12 have too limited support for `<format`, so GCC 13.2+ is required>).
* [CMake](https://cmake.org).


Since we use [Conan](https://conan.io) to install dependencies, the dependencies and their versions are listed and kept up-to-date in the file [`conanfile.txt`](conanfile.txt) under the `[requires]` section.
They are:

- [LibYAML](https://pyyaml.org/wiki/LibYAML)
- [CLI11](https://cliutils.github.io/CLI11/book/)
- [glob](https://github.com/p-ranav/glob)
- [Tree-sitter CUDA C++ language support](https://github.com/tree-sitter-grammars/tree-sitter-cuda)

[Tree-sitter](https://tree-sitter.github.io) is the parser for the C++ source code is parsed with, the same used by many syntax highlighters.

Alternatively, you can install them via your package manager.
Please, be aware that some dependencies are likely available via your package manager, while others are harder to find and may require manual compilation; to avoid this, we suggest [Conan](https://conan.io) (which you can install via `pip install conan`).
If you decide to compile them yourself, you may pass them to CMake via the usual [`<PackageName>_ROOT`](https://cmake.org/cmake/help/latest/variable/PackageName_ROOT.html) variabe on CMake command-line.

## Building and serving the documentation

If you go for Conan and it is already configured (i.e., it has a default profile), from the repo root you can simply run
```shell
# build and install dependencies into the build directory
conan install --build=missing
# configure the project using the 'conan-debug' profile, i.e., the default profile Conan generates
cmake --preset conan-debug -S .
# build Doxydown using all available cores according to the default Conan profile
cmake --build --preset conan-debug -j$(nproc)
```

Dowydown is finally compiled in `build/Debug` and you can invoke it as

```shell
build/Debug/doxydown --help
```

[Material for MkDocs](https://squidfunk.github.io/mkdocs-material/) is recommended for publishing your documentation.
It is distributed as a Python package that you can install with

``` 
pip install mkdocs-material
```


## Quick start

Run, from within your source code directory:

```shell
doxydown init
```

This will create a `doxydown.yaml` configuration file, as well as some additional files for publishing with Material for MkDocs.
To start, it is not necessary to modify any of these.

Build the Markdown:

```shell
doxydown build
```
This will populate the output directory (default: `docs`).

Build the static HTML:

```shell
mkdocs build
```

This will populate the `site` directory.

Serve the HTML:

```shell
mkdocs serve
```

and point your browser to `localhost:8000`.
