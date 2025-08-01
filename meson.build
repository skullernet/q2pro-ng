project('q2pro', 'c',
  license: 'GPL-2.0-or-later',
  version: run_command(find_program('python3'), 'version.py', check: true).stdout().strip(),
  meson_version: '>= 0.60.0',
  default_options: [
    'c_std=c11',
    'buildtype=debugoptimized',
  ],
)

common_src = [
  'src/common/bsp.c',
  'src/common/cmd.c',
  'src/common/cmodel.c',
  'src/common/common.c',
  'src/common/crc.c',
  'src/common/cvar.c',
  'src/common/error.c',
  'src/common/field.c',
  'src/common/fifo.c',
  'src/common/files.c',
  'src/common/hash_map.c',
  'src/common/math.c',
  'src/common/mdfour.c',
  'src/common/msg.c',
  'src/common/net/chan.c',
  'src/common/net/net.c',
  'src/common/prompt.c',
  'src/common/sizebuf.c',
  'src/common/utils.c',
  'src/common/vm/interp.c',
  'src/common/vm/load.c',
  'src/common/vm/module.c',
  'src/common/zone.c',
  'src/shared/shared.c',
]

client_src = [
  'src/client/cgame.c',
  'src/client/console.c',
  'src/client/demo.c',
  'src/client/input.c',
  'src/client/keys.c',
  'src/client/main.c',
  'src/client/parse.c',
  'src/client/refresh.c',
  'src/client/screen.c',
  'src/client/sound/main.c',
  'src/client/sound/mem.c',
  'src/common/async.c',
  'src/server/commands.c',
  'src/server/entities.c',
  'src/server/game.c',
  'src/server/init.c',
  'src/server/main.c',
  'src/server/nav.c',
  'src/server/send.c',
  'src/server/user.c',
  'src/server/world.c',
]

ui_src = [
  'src/client/ui/demos.c',
  'src/client/ui/menu.c',
  'src/client/ui/playerconfig.c',
  'src/client/ui/playermodels.c',
  'src/client/ui/script.c',
  'src/client/ui/servers.c',
  'src/client/ui/ui.c',
]

refresh_src = [
  'src/refresh/draw.c',
  'src/refresh/hq2x.c',
  'src/refresh/images.c',
  'src/refresh/legacy.c',
  'src/refresh/main.c',
  'src/refresh/mesh.c',
  'src/refresh/models.c',
  'src/refresh/qgl.c',
  'src/refresh/shader.c',
  'src/refresh/sky.c',
  'src/refresh/state.c',
  'src/refresh/surf.c',
  'src/refresh/tess.c',
  'src/refresh/texture.c',
  'src/refresh/world.c',
]

server_src = [
  'src/client/null.c',
  'src/server/commands.c',
  'src/server/entities.c',
  'src/server/game.c',
  'src/server/init.c',
  'src/server/main.c',
  'src/server/nav.c',
  'src/server/send.c',
  'src/server/user.c',
  'src/server/world.c',
]

game_src = files(
  'src/bgame/bg_misc.c',
  'src/bgame/bg_pmove.c',
  'src/bgame/bg_slidemove.c',
  'src/bgame/m_flash.c',
  'src/shared/shared.c'
)

subdir('src/game')

cgame_src = files(
  'src/bgame/bg_misc.c',
  'src/bgame/bg_pmove.c',
  'src/bgame/bg_slidemove.c',
  'src/bgame/m_flash.c',
  'src/cgame/cg_consolecmds.c',
  'src/cgame/cg_effects.c',
  'src/cgame/cg_entities.c',
  'src/cgame/cg_events.c',
  'src/cgame/cg_frame.c',
  'src/cgame/cg_main.c',
  'src/cgame/cg_precache.c',
  'src/cgame/cg_predict.c',
  'src/cgame/cg_screen.c',
  'src/cgame/cg_servercmds.c',
  'src/cgame/cg_view.c',
  'src/shared/shared.c',
)

cc = meson.get_compiler('c')

win32 = host_machine.system() == 'windows'
x86 = host_machine.cpu_family() == 'x86'

cpuremap = {
  'x86': win32 ? 'x86' : 'i386',
  'aarch64': 'arm64',
}

cpu = host_machine.cpu_family()
if cpu in cpuremap
  cpu = cpuremap[cpu]
endif

if win32
  sys = cpu.endswith('64') ? 'Win64' : 'Win32'
else
  sys = host_machine.system()
  if sys.endswith('bsd')
    sys = sys.substring(0, -3) + sys.substring(-3).to_upper()
  endif
  sys = sys[0].to_upper() + sys.substring(1)
endif

datadir = '.'
libdir = '.'
homedir = ''

system_wide = not win32 and get_option('system-wide')
if system_wide
  datadir = get_option('prefix') / get_option('datadir') / meson.project_name()
  libdir = get_option('prefix') / get_option('libdir') / meson.project_name()
  homedir = get_option('homedir')
endif

common_args = ['-DHAVE_CONFIG_H']
if win32
  common_args += '-D_USE_MATH_DEFINES'
else
  common_args += '-D_GNU_SOURCE'
endif

engine_args = []
if win32
  engine_args += '-D_WIN32_WINNT=0x0601'
endif

common_link_args = []
exe_link_args = []
dll_link_args = []

if cc.get_argument_syntax() == 'gcc'
  if x86
    add_global_arguments('-msse2', '-mfpmath=sse', language: 'c')
  endif

  test_args = [
    '-Werror=vla',
    '-Wformat-security',
    '-Wno-microsoft-anon-tag',
    '-Wpointer-arith',
    '-Wstrict-prototypes',
    '-fms-extensions',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-fsigned-char',
  ]

  common_args += cc.get_supported_arguments(test_args)
  engine_args += cc.get_supported_arguments(['-Wmissing-prototypes'])

  if win32
    common_args += '-D__USE_MINGW_ANSI_STDIO=1'
    common_link_args += '-Wl,--nxcompat,--dynamicbase'

    if cpu == 'x86_64'
      exe_link_args += '-Wl,--high-entropy-va,--image-base=0x140000000'
      dll_link_args += '-Wl,--high-entropy-va,--image-base=0x180000000'
    endif

    dll_link_args += '-static-libgcc'
  endif
elif cc.get_id() == 'msvc'
  common_args += ['/wd4146', '/wd4244', '/wd4305']
endif

add_project_arguments(common_args, language: 'c')
add_project_link_arguments(common_link_args, language: 'c')

config = configuration_data()

texture_formats = []

fallback_opt = ['default_library=static']

zlib = dependency('zlib',
  fallback:        'zlib-ng',
  required:        get_option('zlib'),
  default_options: fallback_opt + [ 'tests=disabled', 'zlib-compat=true', 'force-sse2=true' ],
)

png = dependency('libpng',
  version:         '>= 1.6.11',
  required:        get_option('libpng'),
  default_options: fallback_opt,
)
if png.found()
  texture_formats += 'png'
endif

config.set10('USE_ZLIB', zlib.found())
config.set10('USE_PNG', png.found())

curl = dependency('libcurl',
  version:         '>= 7.68.0',
  required:        get_option('libcurl'),
  default_options: fallback_opt,
)

if curl.found()
  client_src += 'src/client/http.c'
  config.set10('USE_CURL', true)
endif

sdl2 = dependency('sdl2', required: get_option('sdl2'))
if sdl2.found()
  client_src += 'src/unix/video/sdl.c'
  config.set('USE_SDL', 'USE_CLIENT')
endif

common_deps = [zlib]
client_deps = [png, curl, sdl2]
server_deps = []
game_deps = []

jpeg = dependency('libjpeg',
  required:        get_option('libjpeg'),
  default_options: fallback_opt + [ 'jpeg-turbo=disabled', 'tests=disabled' ]
)
if jpeg.found()
  if jpeg.type_name() == 'internal'
    has_rgba = true
  else
    has_rgba = cc.has_header_symbol('jpeglib.h', 'JCS_EXT_RGBA',
      prefix:       '#include <stdio.h>',
      dependencies: jpeg,
      required:     get_option('libjpeg'))
  endif
  if has_rgba
    client_deps += jpeg
    texture_formats += 'jpg'
    config.set10('USE_JPG', true)
  endif
endif

if get_option('tga')
  texture_formats += 'tga'
endif

openal = dependency('openal',
  version:         '>= 1.18.0',
  required:        get_option('openal'),
  default_options: fallback_opt,
)
if openal.found()
  client_src += [ 'src/client/sound/al.c', 'src/client/sound/qal.c' ]
  client_deps += openal.partial_dependency(compile_args: true, includes: true)
  config.set10('USE_OPENAL', true)
endif

# require FFmpeg >= 5.1.3
avcodec_opt = get_option('avcodec')
avcodec = dependency('libavcodec', version: '>= 59.37.100', required: avcodec_opt)
avformat = dependency('libavformat', version: '>= 59.27.100', required: avcodec_opt)
avutil = dependency('libavutil', version: '>= 57.28.100', required: avcodec_opt)
swresample = dependency('libswresample', version: '>= 4.7.100', required: avcodec_opt)
swscale = dependency('libswscale', version: '>= 6.7.100', required: avcodec_opt)
if avcodec.found() and avformat.found() and avutil.found() and swresample.found() and swscale.found()
  client_src += ['src/client/cin.c', 'src/client/sound/ogg.c']
  client_deps += [avcodec, avformat, avutil, swresample, swscale]
  config.set10('USE_AVCODEC', true)
endif

if win32
  subdir('src/windows')
else
  subdir('src/unix')
endif

if get_option('client-ui')
  client_src += ui_src
  config.set10('USE_UI', true)
  if system_wide
    install_data('etc/q2pro.menu', install_dir: datadir / get_option('base-game'))
  endif
endif

if get_option('software-sound').require(win32 or sdl2.found()).allowed()
  client_src += 'src/client/sound/dma.c'
  if sdl2.found()
    client_src += 'src/unix/sound/sdl.c'
  endif
  config.set10('USE_SNDDMA', true)
elif not openal.found()
  warning('Neither software sound nor OpenAL enabled, this is not supported')
endif

if get_option('save-games')
  common_src += 'src/server/save.c'
  config.set10('USE_SAVEGAMES', true)
endif

if get_option('system-console')
  if not win32
    common_src += 'src/unix/tty.c'
  endif
  config.set10('USE_SYSCON', true)
endif

if get_option('tests')
  common_src += 'src/common/tests.c'
  config.set10('USE_TESTS', true)
endif

if get_option('debug')
  refresh_src += 'src/refresh/debug.c'
endif

executable('q2pro-ng', common_src, client_src, refresh_src,
  dependencies:          common_deps + client_deps,
  include_directories:   'inc',
  gnu_symbol_visibility: 'hidden',
  win_subsystem:         'windows,6.0',
  link_args:             exe_link_args,
  c_args:                ['-DUSE_CLIENT=1', '-DUSE_REF=1', engine_args],
  install:               system_wide,
)

executable('q2proded-ng', common_src, server_src,
  dependencies:          common_deps + server_deps,
  include_directories:   'inc',
  gnu_symbol_visibility: 'hidden',
  win_subsystem:         'console,6.0',
  link_args:             exe_link_args,
  c_args:                ['-DUSE_SERVER=1', engine_args],
  install:               system_wide,
)

shared_library('cgame' + cpu, cgame_src,
  name_prefix:           '',
  dependencies:          game_deps,
  include_directories:   'inc',
  gnu_symbol_visibility: 'hidden',
  link_args:             dll_link_args,
  install:               system_wide,
  install_dir:           libdir / get_option('base-game'),
  override_options:      get_option('game-build-options'),
)

shared_library('game' + cpu, game_src,
  name_prefix:           '',
  dependencies:          game_deps,
  include_directories:   ['inc', 'src/game'],
  gnu_symbol_visibility: 'hidden',
  link_args:             dll_link_args,
  install:               system_wide,
  install_dir:           libdir / get_option('base-game'),
  override_options:      get_option('game-build-options'),
)

config.set('REVISION',            meson.project_version().substring(1).split('~')[0].to_int())
config.set_quoted('VERSION',      meson.project_version())
config.set_quoted('CPUSTRING',    cpu)
config.set_quoted('BUILDSTRING',  sys)
config.set_quoted('BASEGAME',     get_option('base-game'))
config.set_quoted('DEFGAME',      get_option('default-game'))
if not win32
  config.set_quoted('DATADIR',    datadir)
  config.set_quoted('LIBDIR',     libdir)
  config.set_quoted('HOMEDIR',    homedir)
endif
config.set_quoted('R_TEXTURE_FORMATS', ' '.join(texture_formats))
config.set_quoted('VID_GEOMETRY', get_option('vid-geometry'))
config.set_quoted('VID_MODELIST', get_option('vid-modelist'))

config.set10('USE_DEBUG',         get_option('debug'))
config.set10('USE_GLES',          get_option('opengl-es1'))
config.set10('USE_ICMP',          get_option('icmp-errors').require(win32 or cc.has_header('linux/errqueue.h')).allowed())
config.set10('USE_MD3',           get_option('md3'))
config.set10('USE_MD5',           get_option('md5'))
config.set10('USE_TGA',           get_option('tga'))

if host_machine.system() == 'darwin'
  config.set_quoted('DEFGLPROFILE', 'gl3.2')
elif get_option('opengl-es1')
  config.set_quoted('DEFGLPROFILE', 'es1')
else
  config.set_quoted('DEFGLPROFILE', '')
endif

configure_file(output: 'config.h', configuration: config)

if system_wide
  summary({'datadir': datadir, 'libdir': libdir, 'homedir': homedir}, section: 'Directories')
endif

summary({
  'avcodec'            : config.get('USE_AVCODEC', 0) != 0,
  'client-ui'          : config.get('USE_UI', 0) != 0,
  'debug'              : config.get('USE_DEBUG', 0) != 0,
  'icmp-errors'        : config.get('USE_ICMP', 0) != 0,
  'libcurl'            : config.get('USE_CURL', 0) != 0,
  'libjpeg'            : config.get('USE_JPG', 0) != 0,
  'libpng'             : config.get('USE_PNG', 0) != 0,
  'md3'                : config.get('USE_MD3', 0) != 0,
  'md5'                : config.get('USE_MD5', 0) != 0,
  'openal'             : config.get('USE_OPENAL', 0) != 0,
  'save-games'         : config.get('USE_SAVEGAMES', 0) != 0,
  'sdl2'               : config.get('USE_SDL', '') != '',
  'software-sound'     : config.get('USE_SNDDMA', 0) != 0,
  'system-console'     : config.get('USE_SYSCON', 0) != 0,
  'tests'              : config.get('USE_TESTS', 0) != 0,
  'tga'                : config.get('USE_TGA', 0) != 0,
  'wayland'            : config.get('USE_WAYLAND', 0) != 0,
  'windows-crash-dumps': config.get('USE_DBGHELP', 0) != 0,
  'windows-egl'        : config.get('USE_WIN32EGL', 0) != 0,
  'windows-service'    : config.get('USE_WINSVC', '') != '',
  'x11'                : config.get('USE_X11', 0) != 0,
  'zlib'               : config.get('USE_ZLIB', 0) != 0,
  }, section: 'Features', bool_yn: true)
