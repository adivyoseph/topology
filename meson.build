project('topo','c')

add_global_arguments('-O2', language : 'c')

src_files = ['main.c', 'topology.c']

thread_dep = dependency('threads')

executable('topo', sources : src_files ,dependencies : thread_dep)
