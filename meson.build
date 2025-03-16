project('miniclient', 'c', license: 'GPLv3+')

cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required : true)

glib = dependency('glib-2.0', version: '>= 2.56.0')

wlcl_dep = dependency('wayland-client')
wlcur_dep = dependency('wayland-cursor')

wl_mod = import('unstable-wayland')
xml = wl_mod.find_protocol('xdg-shell')
xdg_shell = wl_mod.scan_xml(xml)

executable('framingham', 'framingham.c', xdg_shell, dependencies: [
  math_dep,
  glib,
  wlcl_dep,
  wlcur_dep,
])
