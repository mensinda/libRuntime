example_src = files([
    'll_main.c',
    'runtime.c',
])

example_runtime = executable(
    'raw_runtime', example_src,
    dependencies: [libappimage_dep, sf_dep]
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
