project('opensea-operations', 'c', license: 'MPL-2.0', version: '3.0.5', default_options : ['warning_level=2'])

c = meson.get_compiler('c')

#TODO: Add -Wcast-align=strict and fix these issues to help ensure better portability
#NOTE: -Wsign-conversion can be useful while debugging, but there are numerous places this shows up
#      and it is not useful, so only add it while debugging.
warning_flags = [
#	'-Wcast-align=strict',
#	'-Wsign-conversion',
	'-Wshadow=compatible-local',
	'-Wvla',
	'-Wfloat-equal',
	'-Wnull-dereference',
	'-Wunused-const-variable',
	'-Wduplicated-cond',
	'-Wjump-misses-init',
	'-Wstringop-overflow',
	'-Wlogical-op',
	'-Wshift-overflow=2',
	'-Wdouble-promotion',
	'-Wformat-security',
	'-Wold-style-definition',
    '-Wstrict-prototypes',
    '-Wmissing-declarations',
    '-Wmissing-prototypes'
]

add_project_arguments(c.get_supported_arguments(warning_flags), language : 'c')

if get_option('debug')
  add_project_arguments('-D_DEBUG', language : 'c')
endif

global_cpp_args = []

#if GCC less than 5, need to set -std=gnu99 at minimum. gnu11 became the default in 5, 17 default in 7 or 8.
#TODO: May be able to move to c11/gnu11 instead, but will need to do a lot of testing first
#skipping sunos since this was a compatibility issue that was reported earlier. May be able to find a better way to handle this in the future.
if not (target_machine.system() == 'sunos') and c.get_id().contains('gcc')
  verStr = c.version().split('.')
  if verStr.get(0).to_int() < 5
      global_cpp_args += ['-std=gnu99']
  endif
endif

if target_machine.system() == 'windows'
  global_cpp_args += ['-D_CRT_NONSTDC_NO_DEPRECATE', '-D_CRT_SECURE_NO_WARNINGS']
  global_cpp_args += ['-DSTATIC_OPENSEA_OPERATIONS']
endif

incdir = include_directories('include')

opensea_common = subproject('opensea-common')
opensea_common_dep = opensea_common.get_variable('opensea_common_dep')

opensea_transport = subproject('opensea-transport')
opensea_transport_dep = opensea_transport.get_variable('opensea_transport_dep')

opensea_operations_lib = static_library('opensea-operations', 'src/ata_Security.c', 'src/buffer_test.c', 'src/defect.c', 'src/depopulate.c', 'src/device_statistics.c', 'src/drive_info.c', 'src/dst.c', 'src/firmware_download.c', 'src/format.c', 'src/generic_tests.c', 'src/host_erase.c', 'src/logs.c', 'src/nvme_operations.c', 'src/operations.c', 'src/power_control.c', 'src/reservations.c', 'src/sanitize.c', 'src/sas_phy.c', 'src/seagate_operations.c', 'src/sector_repair.c', 'src/set_max_lba.c', 'src/smart.c', 'src/trim_unmap.c', 'src/writesame.c', 'src/zoned_operations.c', c_args : global_cpp_args, dependencies : [opensea_common_dep, opensea_transport_dep], include_directories : incdir)
opensea_operations_dep = declare_dependency(link_with : opensea_operations_lib, compile_args : global_cpp_args, include_directories : incdir)
