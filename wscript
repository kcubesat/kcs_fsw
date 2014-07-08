#!/usr/bin/env python
# encoding: utf-8

import os
import time
import glob

APPNAME = 'nanomind'
VERSION = time.strftime("%Y.%m", time.gmtime())

top	= '.'
out	= 'build'

# Scan modules and libs in local lib dir
modules = ["lib/libgomspace", "lib/libio", "lib/libcsp", "lib/libfsw_sensor"]
if os.path.exists('lib/libstorage'):
	modules += ['lib/libstorage']
if os.path.exists('lib/libadcs'):
	modules += ['lib/libadcs']
if os.path.exists('lib/libcdh'):
	modules += ['lib/libcdh']

def options(ctx):

	ctx.load('gcc gas gxx')
	if os.path.exists('eclipse.py'):
		ctx.load('eclipse')
	ctx.recurse(modules)
	ctx.add_option('--toolchain', action='store', default='arm-none-eabi-', help='Set toolchain prefix')

	gr = ctx.add_option_group("NanoMind options")
	gr.add_option('--hostname', action='store', default='nanomind', help='Set system hostname')
	gr.add_option('--model', action='store', default='Gomspace A712B', help='Set system model')
	gr.add_option('--rom', action='store_true', help='Compile ROM image')
	gr.add_option('--config-sd-cs', action='store', default=15, help='Chip select for SD-card')
	gr.add_option('--enable-sd', action='store_true', help='With SD-Card (requires FAT)')
	gr.add_option('--enable-df', action='store_true', help='With DataFlash (requires UFFS)')
	gr.add_option('--enable-flash-fs', action='store_true', help='With Flash FS (requires UFFS)')
	gr.add_option('--enable-can', action='store_true', help='With CAN')
	gr.add_option('--enable-cpp', action='store_true', help='Use C++')
	gr.add_option('--enable-task-connless', action='store_true', help='Start demo task: connless')
	gr.add_option('--enable-task-hk', action='store_true', help='Start demo task: housekeeping')
	gr.add_option('--enable-rtc', action='store_true', help='Enable NanoMind A712C RTC')
	gr.add_option('--enable-mpio', action='store_true', help='Enable NanoMind A712D MPIO')
	gr.add_option('--with-storage', action='store_true', help='Enable Storage module')
	gr.add_option('--with-adcs', action='store_true', help='Enable ADCS module')
	gr.add_option('--with-cdh', action='store_true', help='Enable CDH module')

def configure(ctx):
	ctx.env.CC = ctx.options.toolchain + "gcc"
	ctx.env.CXX = ctx.options.toolchain + "g++"
	ctx.env.AR = ctx.options.toolchain + "ar"
	ctx.env.AS = ctx.options.toolchain + "gcc"
	ctx.env.SIZE = ctx.options.toolchain + 'size'
	ctx.env.OBJCOPY = ctx.options.toolchain + 'objcopy'
	ctx.load('gcc gas gxx')

	ctx.find_program('objcopy', var='OBJCOPY')
	ctx.find_program('size', var='SIZE')
	ctx.find_program('openocd', var='OPENOCD', mandatory=False)

	link_script = '../src/nanomind-rom.ld' if ctx.options.rom else '../src/nanomind-ram.ld'

	ctx.env.append_unique('CFLAGS',		 	['-O2', '-std=gnu99', '-g', '-ffunction-sections', '-mcpu=arm7tdmi', '-Wa,-adhlns=nanomind.lst', '-Wall', '-Wextra', '-Wcast-align', '-Wno-unused-parameter', '-Wno-missing-field-initializers'])
	ctx.env.append_unique('FILES_NANOMIND',		['src/*.c', 'src/crt.s', 'src/*.cpp'])
	ctx.env.append_unique('EXCLUDES_NANOMIND',	['.cproject'])
	ctx.env.append_unique('ASFLAGS_NANOMIND',	['-xassembler-with-cpp', '-nostartfiles', '-c'])
	ctx.env.append_unique('LINKFLAGS_NANOMIND',	['-T{0}'.format(link_script), '-nostartfiles', '-mcpu=arm7tdmi', '-Xlinker', '--gc-sections', '-Wl,-Map,nanomind.map'])

	# For a pre-built libs, make a list here.
	# Possible libs: ['adcs', 'cdh', 'io', 'csp', 'storage', 'gomspace']
	ctx.env.LIBS = ['io', 'csp', 'gomspace']

	#****************************#
	# Override/set options BEGIN #
	#****************************#

	# General options
	ctx.options.arch = 'arm'

	# Options for libIO
	ctx.options.enable_ftp_server = True
	ctx.options.enable_nanomind_client = True
	ctx.options.enable_nanocom_client = True
	ctx.options.enable_nanocam_client = True
	ctx.options.enable_nanopower2_client = True
	ctx.options.enable_nanohub_client = True
	ctx.options.enable_csp_client = True

	# Options for LibGomspace
	ctx.options.enable_supervisor = True
	ctx.options.enable_gosh = True

	# Options for LibCSP
	ctx.options.with_os = 'freertos'
	ctx.options.with_freertos = '../libgomspace/include'
	ctx.options.with_freertos_config = 'src/conf_freertos.h'
	ctx.options.enable_rdp = True
	ctx.options.enable_crc32 = True
	ctx.options.enable_hmac = True
	ctx.options.enable_xtea = True
	ctx.options.enable_qos = True
	ctx.options.with_drivers = '../libgomspace/include'
	ctx.options.enable_if_kiss = True
	ctx.options.enable_if_i2c = True

	# Options for libCDH
	if ctx.options.with_cdh:
		ctx.define('WITH_CDH', '1')
		ctx.env.LIBS = ['cdh'] + ctx.env.LIBS
		ctx.options.enable_rsh_client = True
		ctx.options.enable_rsh_server = True
		if hasattr(ctx.options, 'enable_uffs') and ctx.options.enable_uffs:
			ctx.options.enable_rrstore = True

	# Options for libADCS
	if ctx.options.with_adcs:
		ctx.env.LIBS = ['adcs'] + ctx.env.LIBS
		ctx.define('WITH_ADCS', '1')
		ctx.options.enable_adcs = True
		ctx.options.enable_adcs_client = True

	# Options for libStorage
	if ctx.options.with_storage:
		ctx.options.enable_fat = True
		ctx.options.enable_uffs = True
		ctx.define('WITH_STORAGE', '1')
		ctx.env.LIBS = ['storage'] + ctx.env.LIBS
		ctx.define_cond('ENABLE_SD', ctx.options.enable_sd)
		ctx.define_cond('ENABLE_FLASH_FS', ctx.options.enable_flash_fs)
		ctx.define_cond('ENABLE_DF', ctx.options.enable_df)
	else:
		ctx.options.enable_fat = False
		ctx.options.enable_uffs = False

	#**************************#
	# Override/set options END #
	#**************************#
	
	# Process options for NanoMind board
	ctx.define('CONFIG_HOSTNAME', ctx.options.hostname)
	ctx.define('CONFIG_MODEL', ctx.options.model)
	ctx.define('CONFIG_SD_CS', int(ctx.options.config_sd_cs))

	ctx.define_cond('ENABLE_CAN', ctx.options.enable_can)
	ctx.define_cond('ENABLE_CPP', ctx.options.enable_cpp)
	ctx.define_cond('ENABLE_TASK_CONNLESS', ctx.options.enable_task_connless)
	ctx.define_cond('ENABLE_TASK_HK', ctx.options.enable_task_hk)
	ctx.define_cond('ENABLE_RTC', ctx.options.enable_rtc)
	ctx.define_cond('ENABLE_MPIO', ctx.options.enable_mpio)
	if ctx.options.enable_can:
		ctx.options.with_driver_can = 'at91sam7a1'
		ctx.options.enable_if_can = True
	ctx.env.ROM = ctx.options.rom
	ctx.define_cond('OBC_ROM', ctx.options.rom)

	ctx.write_config_header('include/conf_nanomind.h', top=True, remove=True)

	ctx.recurse(modules)

def build(ctx):
	ctx(export_includes='include', name='include')
	ctx.recurse(modules)
	ctx.program(
		source = ctx.path.ant_glob(ctx.env.FILES_NANOMIND, excl=ctx.env.EXCLUDES_NANOMIND),
		features = 'asm',
		target = 'nanomind.elf', 
		includes = 'include', 
		asflags = ctx.env.ASFLAGS_NANOMIND,
		defines = ctx.env.DEFINES_NANOMIND,
		linkflags = ctx.env.LINKFLAGS_NANOMIND,
		use = ['adcs', 'cdh', 'io', 'csp', 'storage', 'gomspace', 'fsw_sensor'],
		libpath = '../lib/',
		lib = ctx.env.LIBS + ['m'])
	ctx(rule='${OBJCOPY} -O binary -j .text -j .data -j.relocate -j.sram ${SRC} ${TGT}', source='nanomind.elf', target='nanomind.bin', name='objcopy')
	ctx(rule='${SIZE} --format=berkeley ${SRC}', source='nanomind.elf', always=True, name='size')

def dist(ctx):
	ctx.excl = '**/build/* **/.* **/*.pyc **/*.o **/*~ *.tar.gz *.tar.bz2 lib/libgomspace/include/dev/ap7 lib/libgomspace/include/dev/avr32 lib/libgomspace/include/dev/x86 lib/libgomspace/src/dev-avr32 lib/libgomspace/src/dev-avr8 lib/libgomspace/src/dev-pc nanomind*'
	if (not ctx.options.with_storage == True or ctx.options.install_storage):
		ctx.excl += ' lib/libstorage'
	if (not ctx.options.with_cdh == True or ctx.options.install_cdh):
		ctx.excl += ' lib/libcdh'
	if (not ctx.options.with_adcs == True or ctx.options.install_adcs):
		ctx.excl += ' lib/libadcs'

def program(ctx):
	if not ctx.env.OPENOCD:
		ctx.fatal('Target programming required openocd (http://openocd.sourceforge.net/)')
	if not ctx.env.ROM:
		ctx.fatal('Image must be configured with --rom')
	ctx(rule='${OPENOCD} -s ../jtag -f ../jtag/openocd.cfg', source='nanomind.elf', name='openocd', always=True)

from waflib.Build import BuildContext
class Program(BuildContext):
    cmd = 'program'
    fun = 'program'

def upload(ctx):
	if ctx.env.ROM:
		ctx.fatal("Image must not be configured with --rom")
	ctx(rule='ftp-client -f nanomind.bin -R -s 240', source='nanomind.elf', name='ftp-client', always=True)

from waflib.Build import BuildContext
class Upload(BuildContext):
    cmd = 'upload'
    fun = 'upload'
