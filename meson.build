project('opensea-operations', 'c', license: 'MPL-2.0', version: '2.0.2')

if get_option('debug')
  add_project_arguments('-D_DEBUG', language : 'c')
endif

global_cpp_args = []

if target_machine.system() == 'windows'
  global_cpp_args += ['-DSTATIC_OPENSEA_OPERATIONS']
endif

incdir = include_directories('include')

opensea_common = subproject('opensea-common')
opensea_common_dep = opensea_common.get_variable('opensea_common_dep')

opensea_transport = subproject('opensea-transport')
opensea_transport_dep = opensea_transport.get_variable('opensea_transport_dep')

opensea_operations_lib = static_library('opensea-operations', 'src/ata_Security.c', 'src/buffer_test.c', 'src/defect.c', 'src/depopulate.c', 'src/device_statistics.c', 'src/drive_info.c', 'src/dst.c', 'src/firmware_download.c', 'src/format.c', 'src/generic_tests.c', 'src/host_erase.c', 'src/logs.c', 'src/nvme_operations.c', 'src/operations.c', 'src/power_control.c', 'src/reservations.c', 'src/sanitize.c', 'src/sas_phy.c', 'src/seagate_operations.c', 'src/sector_repair.c', 'src/set_max_lba.c', 'src/smart.c', 'src/trim_unmap.c', 'src/writesame.c', 'src/zoned_operations.c', c_args : global_cpp_args, dependencies : [opensea_common_dep, opensea_transport_dep], include_directories : incdir)
opensea_operations_dep = declare_dependency(link_with : opensea_operations_lib, compile_args : global_cpp_args, include_directories : incdir)