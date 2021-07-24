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
