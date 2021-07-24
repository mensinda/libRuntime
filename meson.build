project('modularRuntime', ['c'],
  version : '0.0.1',
  license : 'MIT',
  meson_version : '>=0.58.0',
  default_options : [
    'b_lto=False',
    'default_library=static',

    'liblzma:warning_level=0',
    'lz4:warning_level=0',
    'zlib:warning_level=0',
    'zstd:warning_level=0',
    'zstd:bin_programs=false',
  ],
)

add_project_link_arguments('-static', language: 'c')

cc = meson.get_compiler('c')

sf_sp = subproject(
  'squashfuse',
  default_options: [
    'warning_level=0',
    'use_lzo=disabled',
    'enable_demo=false',
    'fuse_version=2',
  ],
)

sf_dep = sf_sp.get_variable('libsquashfuse_ll_dep')

dd_prog      = find_program('dd')
objcopy_prog = find_program('objcopy')
patcher_prog = find_program('patch_binary.sh')

subdir('lib')
subdir('src')
