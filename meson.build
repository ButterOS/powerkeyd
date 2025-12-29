
project(
  'powerkeyd',
  'c',
  version: '0.1.0',
  default_options: [
    'warning_level=2',
    'c_std=c11',
    'prefix=/usr',
  ],
  meson_version : '>= 0.60'
)

libevdev_dep = dependency('libevdev', required: true)
systemd = dependency('systemd')

conf = configuration_data()
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('DEFAULT_MATCH', get_option('default_match'))
conf.set('DEFAULT_GRAB', get_option('grab_device') ? '1' : '0')

configure_file(output: 'config.h', configuration: conf)
configuration_inc = include_directories('.')

subdir('src')

install_data('scripts/displayctl', install_dir: get_option('bindir'), install_mode: 'rwxr-xr-x')
install_data('scripts/lockctl', install_dir: get_option('bindir'), install_mode: 'rwxr-xr-x')

override_unitdir = get_option('systemd_system_unit_dir')
systemd_unit_dir = override_unitdir != '' \
  ? override_unitdir \
  : systemd.get_variable(pkgconfig: 'systemdsystemunitdir')

install_data(
  'systemd/powerkeyd.service',
  'systemd/os-lock.service',
  'systemd/os-dim.service',
  'systemd/os-screenoff.service',
  'systemd/os-wake.service',
  'systemd/os-lock.timer',
  'systemd/os-dim.timer',
  'systemd/os-screenoff.timer',
  install_dir: systemd_unit_dir,
  install_mode: ['rw-r--r--', 0, 0],
  install_tag: 'systemd-unit'
)