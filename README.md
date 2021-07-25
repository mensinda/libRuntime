# libRuntime

The primary goal of this repository is to provide a framework for creating
statically linked [AppImage](https://appimage.org) runtimes.

The code from this repository is currently a copy-paste of the
[AppImageKit runtime](https://github.com/AppImage/AppImageKit/tree/master/src)
with some refactoring (including fixing all the compiler warnings).

## Building

To build the default, completely statically linked runtime, pass
`--wrap-mode=forcefallback` to the Meson setup command. This causes Meson to
pull all dependencies as subprojects and build them from source (as static
libraries).

```bash
meson --wrap-mode=forcefallback -Ddefault_library=static build
ninja -C build
```

To reduce the file size of the generated runtime image, the use of
[musl libc](https://musl.libc.org) is recommended.

## Using libRuntime to build a custom AppImage runtime

To use libRuntime for your runtime, generate a `.wrap` file for this project
and include it in your subproject folder. Then the libRuntime library as well
as the patch program for the executable can be obtained like this:

```meson
# more code ...

# Required programs for the patcher script
dd_prog      = find_program('dd')
objcopy_prog = find_program('objcopy')

# Build libRuntime
libruntime_sp  = subproject('libruntime')
libruntime_dep = libruntime_sp.get_variable('libruntime_dep')
patcher_prog   = libruntime_sp.get_variable('patcher_prog')

# Build the unpatched executable
foo_raw = executable(
    'foo_raw', ['foo.c'],
    dependencies: [libruntime_dep],
)

# Patch the executable
custom_target(
    'patch_runtime',
    input:       [foo_raw],
    output:      ['foo'],
    install:     true,
    install_dir: get_option('bindir'),
    command: [
        patcher_prog,
        '-i', '@INPUT@',
        '-o', '@OUTPUT@',
        '-p', '@PRIVATE_DIR@',
        '--dd', dd_prog,
        '--objcopy', objcopy_prog,
    ],
)

# more code ...
```

See the [default runtime code](https://github.com/mensinda/libRuntime/blob/master/src/runtime.c)
for how to use libRuntime.
