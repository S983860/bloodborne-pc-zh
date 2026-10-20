#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p out
if [[ -z ${CC:-} ]]; then
    CC=$(command -v cc || command -v gcc || true)
    if [[ -z $CC ]]; then
        for candidate in /nix/store/*-gcc-wrapper-*/bin/gcc; do
            if [[ -x $candidate ]]; then CC=$candidate; break; fi
        done
    fi
fi
if [[ -z ${CC:-} ]]; then echo 'Install GCC/Clang or set CC.' >&2; exit 1; fi
# Dependencies come from pkg-config (Vulkan loader/headers, SDL3). On NixOS the
# environment is provided by shell.nix; re-enter it automatically if needed.
if ! { command -v pkg-config >/dev/null && pkg-config --exists vulkan sdl3 && command -v cmake >/dev/null && command -v ninja >/dev/null; }; then
    if [[ -z ${BB_IN_NIX_SHELL:-} ]] && command -v nix-shell >/dev/null; then
        exec env BB_IN_NIX_SHELL=1 nix-shell shell.nix --run "bash build.sh $*"
    fi
    echo 'Need pkg-config with vulkan and sdl3, cmake and ninja (see shell.nix).' >&2; exit 1
fi
read -r -a includes <<< "$(pkg-config --cflags vulkan sdl3)"
read -r -a libraries <<< "$(pkg-config --libs vulkan sdl3)"
# Windows: MSYS2 CLANG64 shell (README "Windows": clang, libc++ and lld, whose threads and locks
# are the Win32 ones). The GPU library is static there (a DLL cannot leave the runtime_* symbols
# to the executable), the runtime adds its Win32 files (src/win32_*.c), runs without pthreads,
# and the executable keeps a console and bottom-up addresses (guest-visible host memory must
# stay below 1 TiB).
windows=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) windows=1 ;; esac
cstd=(-std=c11)
threads=(-pthread)
if [[ -n $windows ]]; then
    threads=()
    libraries=("${libraries[@]/-mwindows/-mconsole}")
    cstd=(-std=gnu11 -D_FILE_OFFSET_BITS=64 -D_WIN32_WINNT=0x0A00)
fi
# BB_JOBS=N: at most N compile and link-time optimization jobs (less memory, e.g. while the
# game runs).
# GPU library (shadPS4 video core + drivers), built by CMake into out/gpu/libbbgpu.so.
# BB_PGO: generate (instrumented build that writes pgo/ while the game runs), use, off.
# Default: use the profile in pgo/ when there is one. BB_LTO=OFF disables link-time optimization.
# Clang (Windows CLANG64) writes pgo/default_*.profraw: merged here into pgo/default.profdata,
# which the "use" build reads (GCC: pgo/*.gcda, read as they are).
mkdir -p pgo
if [[ ${BB_PGO:-} != generate && -n $(find pgo -name '*.profraw' -print -quit 2>/dev/null) &&
      ( ! -f pgo/default.profdata || -n $(find pgo -name '*.profraw' -newer pgo/default.profdata -print -quit) ) ]]; then
    profdata=$(command -v llvm-profdata || true)
    if [[ -n $profdata ]]; then
        "$profdata" merge -o pgo/default.profdata pgo/*.profraw
        echo "PGO: merged $(find pgo -name '*.profraw' | wc -l) raw profile(s) into pgo/default.profdata"
    else
        echo 'PGO: pgo/*.profraw found but no llvm-profdata in PATH; not merged' >&2
    fi
fi
pgo=${BB_PGO:-}
if [[ -z $pgo ]]; then
    if [[ -n $(find pgo \( -name '*.gcda' -o -name 'default.profdata' \) -print -quit 2>/dev/null) ]]; then
        pgo=use
    else
        pgo=off
    fi
fi
# Submodules (git clone --recursive, or: git submodule update --init) and this port's changes
# to FSR-Vulkan (gpu/patches/fsr-vulkan), applied to its working tree once.
if [[ ! -f gpu/third_party/fsr-vulkan/CMakeLists.txt || ! -f gpu/third_party/imgui/imgui.h ]]; then
    git submodule update --init --recursive
fi
# A plain source copy (no submodule checkout) already has them applied.
if [[ -e gpu/third_party/fsr-vulkan/.git ]]; then
    for patch in gpu/patches/fsr-vulkan/*.patch; do
        if ! git -C gpu/third_party/fsr-vulkan apply --reverse --check "$PWD/$patch" 2>/dev/null; then
            git -C gpu/third_party/fsr-vulkan apply "$PWD/$patch"
        fi
    done
fi
# Line tables only (not full debug info): crash offsets still resolve to function and
# file:line with llvm-symbolizer, and compiling and linking take much less time.
gpu_config=(-G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBB_PGO="$pgo" -DBB_LTO="${BB_LTO:-ON}"
            -DBB_PGO_DIR="$PWD/pgo" -DBB_MARCH="${BB_MARCH:-}"
            "-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O2 -gline-tables-only -DNDEBUG"
            "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -gline-tables-only -DNDEBUG")
if [[ -n $windows && -n ${MINGW_PREFIX:-} ]]; then
    # Never mix MSVC package-registry libraries with CLANG64's libc++ ABI.
    gpu_config+=("-DCMAKE_PREFIX_PATH=$MINGW_PREFIX"
                -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
                -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
                "-DProtobuf_DIR=$MINGW_PREFIX/lib/cmake/protobuf"
                "-Dabsl_DIR=$MINGW_PREFIX/lib/cmake/absl"
                "-Dutf8_range_DIR=$MINGW_PREFIX/lib/cmake/utf8_range")
fi
# Configured once per set of options (ninja itself reruns CMake when a CMakeLists.txt changes).
if [[ ! -f out/gpu/build.ninja || $(cat out/gpu/bb-configure.txt 2>/dev/null) != "${gpu_config[*]}" ]]; then
    cmake -S gpu -B out/gpu "${gpu_config[@]}" >/dev/null
    printf '%s' "${gpu_config[*]}" > out/gpu/bb-configure.txt
fi
echo "GPU library: PGO $pgo, LTO ${BB_LTO:-ON}, march ${BB_MARCH:-default}"
# A failed GPU build must stop here: an older libbbgpu.so would otherwise be used silently.
nr_target=()
if [[ -n $windows ]]; then nr_target=(bb-nr-forwarder); fi
if ! ninja -C out/gpu ${BB_JOBS:+-j "$BB_JOBS"} bbgpu bb-engine-assets "${nr_target[@]}" > out/gpu-build.log 2>&1; then
    grep -v '^\[' out/gpu-build.log | tail -40 >&2
    echo 'GPU library build failed (full log: out/gpu-build.log)' >&2; exit 1
fi
if [[ -n $windows ]]; then cp out/gpu/nvngx.dll_bbpc_nr.dll out/nvngx.dll_bbpc_nr.dll; fi
cp -f out/gpu/bb-engine-assets* out/
# $ORIGIN/gpu: packaged copies keep the library next to the binary without patching it.
gpu=(-Lout/gpu -lbbgpu -Wl,-rpath,'$ORIGIN/gpu' -Wl,-rpath,"$PWD/out/gpu" -rdynamic)
runtime=(src/runtime*.c)
link=(-no-pie)
if [[ -n $windows ]]; then
    # out/gpu/bbgpu_link.txt (gpu/CMakeLists.txt): libbbgpu.a and its dependencies, one per line.
    mapfile -t gpu < out/gpu/bbgpu_link.txt
    runtime+=(src/win32_*.c)
    # The static library's link-time optimization (ThinLTO in lld) runs here; on Linux it runs in
    # the .so link.
    # The ThinLTO cache keeps each module's optimized code: a rebuild after a small change
    # optimizes only the modules it touched (and what inlines from them), on every thread.
    link=(-Wl,--disable-dynamicbase,--disable-high-entropy-va -lwinmm -lws2_32 -lpsapi -flto=thin
          -Wl,--thinlto-cache-dir="$PWD/out/thinlto-cache" -Wl,--thinlto-jobs="${BB_JOBS:-all}"
          -Wl,--thinlto-cache-policy=cache_size_bytes=6g:prune_after=1209600s)
fi
# Third-party decoders: compiled once, without this project's -Werror policy.
atrac9=(third_party/LibAtrac9/C/src/*.c)
if [[ ! -f out/libatrac9.a || -n $(find third_party/LibAtrac9/C/src -newer out/libatrac9.a -name '*.c') ]]; then
    rm -rf out/atrac9 && mkdir -p out/atrac9
    for source in "${atrac9[@]}"; do "$CC" -std=c99 -O2 -g -w -c "$source" -o "out/atrac9/$(basename "${source%.c}").o"; done
    ar rcs out/libatrac9.a out/atrac9/*.o
fi
# Windows: the link includes the GPU library's link-time optimization (a minute or more), and
# run.bat builds before every start, so an up-to-date executable is kept.
gpu_inputs=(out/gpu/libbbgpu.a out/gpu/bbgpu_link.txt)
if [[ -n $windows ]]; then
    # Game/settings paths can contain Chinese on a portable installation.
    if [[ ! -f out/windows-utf8.o || tools/engine/windows_utf8.manifest -nt out/windows-utf8.o ||
          tools/engine/windows_utf8.rc -nt out/windows-utf8.o ]]; then
        windres -I tools/engine tools/engine/windows_utf8.rc -O coff -o out/windows-utf8.o
    fi
    link+=(out/windows-utf8.o)
    gpu_inputs+=(out/windows-utf8.o)
    # Native bridge dependencies can change without changing libbbgpu.a itself.
    for dependency in "${gpu[@]}"; do
        [[ ! -f $dependency ]] || gpu_inputs+=("$dependency")
    done
fi
if [[ -n $windows && -f out/bb-probe.exe && -f out/bb-gpu-capabilities.exe &&
      -z $(find src gpu/bbgpu.h "${gpu_inputs[@]}" out/libatrac9.a build.sh \
               tools/gpu_capabilities.c -newer out/bb-probe.exe -print -quit) ]]; then
    echo "Up to date: $PWD/out/bb-probe.exe"
else
"$CC" "${cstd[@]}" -O2 -g -Wall -Wextra -Werror "${threads[@]}" "${includes[@]}" -I. -Isrc src/probe.c "${runtime[@]}" src/vulkan_smoke.c out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" "${link[@]}" -o out/bb-probe
echo "Built $PWD/out/bb-probe"
# GPU and controller inventory; keep the Windows C dialect and include SDL headers.
"$CC" "${cstd[@]}" -O2 -Wall -Wextra -Werror "${includes[@]}" tools/gpu_capabilities.c "${libraries[@]}" -o out/bb-gpu-capabilities
fi
if [[ ${1:-} == --test ]]; then
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${includes[@]}" -I. -Isrc tests/test_pad.c "${libraries[@]}" -o out/pad-test
    out/pad-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_runtime.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/runtime-test
    out/runtime-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -Isrc tests/test_file_mods.c -o out/file-mods-test
    out/file-mods-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_sema.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/sema-test
    out/sema-test
    "$CC" -std=c11 -D_GNU_SOURCE -O2 -g -Wall -Wextra -Werror -I. -Isrc tests/test_content.c src/runtime_content.c -o out/content-test
    out/content-test
fi
