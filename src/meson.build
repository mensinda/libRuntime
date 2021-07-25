example_src = files([
    'runtime.c',
])

ver = meson.project_version()
example_runtime = executable(
    'raw_runtime', example_src,
    c_args: [f'-DPROJECT_VERSION="@ver@"'],
    dependencies: [libruntime_dep],
)

custom_target(
    'patch_runtime',
    input:       [example_runtime],
    output:      ['runtime'],
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
