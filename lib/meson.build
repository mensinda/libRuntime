libappimage_src = files([
    'libappimage/digest.c',
    'libappimage/elf.c',
    'libappimage/hexlify.c',
    'libappimage/md5.c',
])

libruntime_src = files([
    'detect.c',
    'extract.c',
    'll_main.c',
    'mount.c',
    'run.c',
    'util.c',
])

libruntime = static_library(
    'libruntime', [libruntime_src + libappimage_src],
    dependencies: [sf_dep],
)

libruntime_dep = declare_dependency(
    link_with: [libruntime],
    include_directories: include_directories('.'),
    dependencies: [sf_dep],
)
