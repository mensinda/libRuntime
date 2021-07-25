libappimage_src = files([
    'libappimage/digest.c',
    'libappimage/elf.c',
    'libappimage/hexlify.c',
    'libappimage/md5.c',
])

libappimage = static_library('libappimage', libappimage_src)

libappimage_dep = declare_dependency(
    link_with: libappimage,
    include_directories: include_directories('.'),
)

libruntime_src = files([
    'detect.c',
    'extract.c',
    'll_main.c',
    'mount.c',
    'run.c',
    'util.c',
])

libruntime = static_library(
    'libruntime', libruntime_src,
    dependencies: [sf_dep],
    c_args: ['-Werror'],
    link_with: [libappimage],
)

libruntime_dep = declare_dependency(
    link_with: [libruntime, libappimage],
    include_directories: include_directories('.'),
    dependencies: [sf_dep],
)
