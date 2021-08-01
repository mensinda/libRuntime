project('modularRuntime', ['c'],
  version : '0.0.2',
  license : 'MIT',
  meson_version : '>=0.58.0',
  default_options : [
    'b_lto=true',
    'default_library=static',
    'warning_level=3',
    'werror=true',
    'debug=false',
    'optimization=s',

    'fuse:warning_level=0',
    'fuse3:warning_level=0',
    'liblzma:warning_level=0',
    'lz4:warning_level=0',
    'zlib:warning_level=0',
    'zstd:warning_level=0',

    'fuse:werror=false',
    'fuse3:werror=false',
    'liblzma:werror=false',
    'lz4:werror=false',
    'zlib:werror=false',
    'zstd:werror=false',
  ],
)

add_project_link_arguments('-static', language: 'c')
add_project_arguments('-DFUSE_USE_VERSION=26', language: 'c')

cc = meson.get_compiler('c')

sf_sp = subproject(
  'squashfuse',
  default_options: [
    'warning_level=0',
    'werror=false',
    'use_lzo=disabled',
    'enable_demo=false',
    'fuse_version=2',
  ],
)

sf_dep = sf_sp.get_variable('libsquashfuse_ll_dep').as_system('system')

dd_prog      = find_program('dd')
objcopy_prog = find_program('objcopy')
patcher_prog = find_program('patch_binary.sh')

subdir('lib')
subdir('src')
