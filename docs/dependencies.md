# Dependencies and platform setup

## Required toolchain

- GCC 13+ or Clang 18+ with a C++20 standard library, C compiler, and pthreads.
- CMake **3.30+** (Aeron 1.48.0 requires this); the optional local bootstrap
  pins **3.31.6**.
- Ninja and Git.
- Java **17+** runtime for the SBE generator, not the native driver. Ubuntu
  setup selects OpenJDK 17; a JDK is not required.
- clang-format **18** for formatting checks.
- Bash and GNU coreutils, including `timeout`, `mktemp`, and `realpath`.
- curl, CA certificates, and internet access to GitHub and Maven Central on
  first configuration, or the dependency cache described under [offline builds](#offline-builds).

No Python, Prometheus, Grafana, Terraform, VirtualBox, Java Media Driver, or
system-installed Aeron is required to build/run these tools. Native driver
optional BSD/UUID libraries are discovered by upstream; their fallbacks suffice.

## Platform setup

### Linux

The reference environment is **Ubuntu 24.04 LTS, Linux x86-64**. Other
distributions must supply equivalent tools and are not validated. Linux arm64
is an intended later validation target, not a verified platform.

From the repository root, install the shared local/CI package list,
[requirements-ubuntu.txt](../requirements-ubuntu.txt):

```bash
sudo apt-get update
xargs -r -a requirements-ubuntu.txt sudo apt-get install -y --no-install-recommends
```

This is an apt package list, not a pip requirements file. These commands change
system packages and may prompt for your sudo password. The list selects Java 17
and clang-format 18 explicitly; Ubuntu supplies package patch updates.

### Windows (WSL2)

Use WSL2 with Ubuntu 24.04, then follow the Linux instructions inside WSL.
Keep the checkout in the Linux filesystem (for example, `~/src/`), not `/mnt/c/`.
Run configuration, builds, and tests in the WSL shell. Native Windows builds
are not supported by this foundation.

### macOS (Linux VM)

Use an Ubuntu 24.04 Linux VM and follow the Linux instructions in the guest.
Keep the checkout and run all build/test commands inside that VM. Native macOS
builds are not supported by this foundation; installing tools with Homebrew
alone does not establish compatibility.

Apple Silicon typically uses arm64 guests. Linux arm64 remains unverified;
Ubuntu x86-64 CI results do not validate arm64. Choose VM software compatible
with the host OS, CPU architecture, and hypervisor setup; VirtualBox is not
required.

### CMake setup inside Linux

Ubuntu 24.04's default CMake is below the project's minimum. If your installed
version is below 3.30, use the optional repository-local binary bootstrap
(no sudo), then set its path:

```bash
bash scripts/local_cmake.sh
export PATH="$PWD/.tools/cmake-3.31.6-linux-$(uname -m)/bin:$PATH"
```

## Verify the setup

Inside Linux, from the repository root:

```bash
ninja --version
java -version
cmake --version
bash scripts/format.sh --check
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure --no-tests=error
```

Repeat configure/build/test with `release` and `asan` for the full CI matrix.
CTest includes unit and native-driver IPC integration tests. CMake fetches the
pinned dependencies below and generates codecs in the build tree automatically;
do not install these libraries globally or hand-edit generated codecs. See the
[README](../README.md#clean-checkout-build-and-tests) for additional build
commands and the [verification record](verification.md) for results and limits.

## Dependency pins and build sources

Inspected upstream build documentation and the pinned source files on 2026-10-27.
These are deliberate reproducible baseline versions, not claims to be the newest
releases. Updates require a separate review and rerun of all correctness checks.

| Dependency | Pin | Acquisition/source |
| --- | --- | --- |
| Aeron native C driver + C++ wrapper/C client | 1.48.0, commit `d800d60405738dc254eb47a6e0a14e4b86387981` | CMake FetchContent from https://github.com/aeron-io/aeron.git |
| GoogleTest | 1.16.0, commit `6910c9d9165801d8827d628cb72eb7ea9dd538c5` | CMake FetchContent from https://github.com/google/googletest.git |
| SBE generator | `uk.co.real-logic:sbe-all:1.35.0` | CMake downloads https://repo.maven.apache.org/maven2/uk/co/real-logic/sbe-all/1.35.0/sbe-all-1.35.0.jar |
| CMake optional local bootstrap | 3.31.6 | https://github.com/Kitware/CMake/releases/tag/v3.31.6; official binary archive and release SHA-256 list |
| Formatter | clang-format 18 | Ubuntu 24.04 `clang-format-18` package |
| CI checkout | v4.2.2, `11bd71901bbe5b1630ceea73d27597364c9af683` | https://github.com/actions/checkout |
| CI artifact upload | v4.6.2, `ea165f8d65b6e75b540449e92b4886f43607fa02` | https://github.com/actions/upload-artifact |

One build dependency strategy: **CMake owns acquisition in the build tree**.
Source libraries use immutable Git commits. The Java generator uses its explicit
Maven release artifact over verified HTTPS, avoiding a Gradle build and its
additional build-time dependency graph. There are no moving branch references,
package-manager integrations, global library installs, or vendored generated
headers. The generator JAR is a build tool, not linked runtime code.

The SBE JAR pin is a versioned artifact, not an independently committed content
checksum. Its SHA-256 is recorded after acquisition in
`build/<preset>/dependency-manifest.txt`. This detects which bytes a run used;
it does not independently authenticate a first download or an offline override.
Review and lock an independently verified digest when the team establishes its
artifact mirror. Do not interpret that manifest as a supply-chain signature.
The local CMake bootstrap checks the matching official release checksum via TLS.
System compiler, JRE, Ninja and OS package patch levels are not frozen; record
actual versions with experimental results.

Aeron's C++ wrapper is header-only and links the C client (`aeron_static`), not
the deprecated standalone C++ implementation. `aeronmd_s` statically links the
native driver library (system libc/pthread/etc. remain native requirements).
Aeron tests, examples, docs, and Archive are disabled; no Java Media Driver or
Aeron Java/Gradle build is needed. Generated application codecs need Java 17+.
GoogleTest is acquired only when `BUILD_TESTING=ON`. Upstream targets are system
includes and excluded from the default build unless needed by our targets.

HdrHistogram is deferred. The foundation only reports count/min/mean/max with a
non-overflowing floating accumulator for nanosecond sums; it stores no histogram
and reports no percentiles. Roadmap task 6 explicitly adds HdrHistogram, range
configuration, overflow accounting, open-loop scheduling, and distribution tests.
Prometheus/Grafana, Vagrant/VirtualBox, Terraform/GCP and Python orchestration
versions will be chosen when those components are implemented. They are not
build prerequisites and no unverified version promises are made for them.

## Offline builds

On a connected machine, obtain the exact pinned source commits and the exact
SBE JAR; transfer them through a trusted local mechanism. Configure using:

```bash
cmake --preset debug \
  -DFETCHCONTENT_SOURCE_DIR_AERON_DEP=/absolute/path/to/aeron-at-pinned-commit \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/absolute/path/to/googletest-at-pinned-commit \
  -DHFT_SBE_JAR=/absolute/path/to/sbe-all-1.35.0.jar
```

Source overrides bypass FetchContent checkout verification: verify each source's
`git rev-parse HEAD` against the table yourself. A populated normal build tree can
also reconfigure without network using `-DFETCHCONTENT_UPDATES_DISCONNECTED=ON`.
Do not set fully-disconnected mode on a fresh cache and expect dependencies to
appear. Generation remains mandatory; an offline JAR and Java are still needed.

## Upstream references used

- [Building Aeron](https://github.com/aeron-io/aeron/wiki/Building-Aeron)
- [Pinned Aeron CMake requirements](https://github.com/aeron-io/aeron/blob/1.48.0/CMakeLists.txt)
- [C++ wrapper target](https://github.com/aeron-io/aeron/blob/1.48.0/aeron-client/src/main/cpp_wrapper/CMakeLists.txt)
- [Native driver README](https://github.com/aeron-io/aeron/blob/1.48.0/aeron-driver/src/main/c/README.md)
- [Aeron 1.48 release and wrapper migration](https://github.com/aeron-io/aeron/releases/tag/1.48.0)
- [SBE tool guide](https://github.com/aeron-io/simple-binary-encoding/wiki/Sbe-Tool-Guide)
- [SBE 1.35.0 release](https://github.com/aeron-io/simple-binary-encoding/releases/tag/1.35.0)
- [Pinned C++ generator](https://github.com/aeron-io/simple-binary-encoding/blob/1.35.0/sbe-tool/src/main/java/uk/co/real_logic/sbe/generation/cpp/CppGenerator.java)

Aeron and SBE retain their upstream licenses; GoogleTest retains its upstream
license. These do not choose a license for the team's own code.
