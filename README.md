# LLVM fork with extras for assignment checking
Fork of LLVM (https://releases.llvm.org/download.html) with additional
modules for Clang-tidy and the Clang Static Analyzer, mainly for use
in automated checking of student assignments written in the C
language.

## Building
Follow the instructions at https://clang.llvm.org/get_started.html,
but ensure cmake is run with
`-DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra"` (instead of just
clang as shown on the page) to enable Clang-tidy. It is also
recommended to set the build type to release instead of the default
(debug), since debug takes longer and requires more space; this is
enabled with -DCMAKE\_BUILD\_TYPE=RELEASE. To summarize, the CMake
command should look like (on Unix-likes):

    cmake -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" -DCMAKE_BUILD_TYPE=RELEASE -G "Unix Makefiles" ../llvm

and with Visual Studio:

    cmake -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" -DCMAKE_BUILD_TYPE=RELEASE -G "Visual Studio 15 2017" -A x64 -Thost=x64 ..\llvm

When running the actual compilation commands after cmake, you should
enable parallel building (`make -j n`, on Unix-likes, where *n* should
be the number of cores available on your CPU; for Visual Studio,
enable "Multi-processor Compilation" found in the project compiler
options).

Ignore the part about adding the binary directory to your executable
path (unless you actually want to use this fork as your default LLVM
installation).

## Running
You can run the program manually from the build directory (inside the
bin/ subdirectory), but the python script run-analyzer.py is provided
to make it easier, since it provides preconfigured settings in
addition to making it easier to switch between different
assignment-specific declarations (see per-assignment declaration
checking below). You can try the example JSON file by running
(assuming your current working directory is examples/):

    $ ../run-analyzer.py -d decl.json decl-test.c

### Options

    -h           Display help message
    -c           Use the local Clang compilation database
    -d <file>    JSON file for declarations (see file format
    -t <file>    Location of Clang-tidy executable
                 (assumed to be in the build directory
                 based on the script location being at
                 the root of the directory)

## Additions
- misc-assignment-main-nocpp: ensure that C++ features are not used,
  which is not always obvious on MSVC.

- misc-assignment-globals: warn against any variables with global
  storage duration, including both variable with global scope as well as
  static variables. Differs from the built-in global variable checker in
  that const-qualified variables are included as well.

- misc-assignment-goto: warn against usages of the GOTO statement. Differs
  from the built-in checker in that forward-jumping or nested loops are
  not excluded.

- misc-assignment-decl-exist: assignment-specific checker that can be
  used to ensure function and struct declarations exist that match a
  certain signature, assuming that their name is known in advance. Uses
  a JSON file of declarations for configuration (see per-assignment
  declaration checking below).

- nullability.MallocNull: Clang Static Analyzer check that verifies
  that the return value of memory allocation functions is explicitly
  checked for NULL. Complements the built-in nullability check (that
  catches explicit NULL dereferencing) to ensure that dereferencing a
  pointer is only done when that pointer is guaranteed to not be NULL.

## Per-assignment declaration checking
Declarations are provided through a JSON file using Clang-tidy options
(see "Configuring Checks" in
https://clang.llvm.org/extra/clang-tidy/Contributing.html). It is
possible to pass these options either in a .clang-tidy file or through
a command line argument (through the key
misc-assignment-decl-exist.DeclFile), but the wrapper run-analyzer.py
provides the -d option to do this in a more concise way without having
to use YAML.

### JSON format
examples/decl.json provides an example configuration file that should
serve as a template for most use cases and is the fastest way to get
started. Declarations are represented as a map from their identifiers
to the actual function/struct declaration. Functions are represented
by a map of strings (type names) to integers (the amount of times they
should occur) and a return type. Structs use the same representation
as function parameters. Note that the keys "functions" and "structs"
can be omitted completely if there are no declarations to check for.

Internally, the types use LLVM's string representation to compare them
with the keys in the JSON object, which means that pointer types have
to be written with exactly one space between the asterisk and the type
name (i.e. write "int \*", not "int\*"). Additionally, array types are
simply written as pointers as well, i.e. the parameter int foo[] is
represented as "int *".

No semantic control is done on the JSON declarations; negative
integers and strings representing invalid C identifiers are allowed
(but will not match anything in the source file, for obvious reasons).
