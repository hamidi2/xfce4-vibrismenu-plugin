sizes = [16, 22, 24, 32, 48, 64, 128, 256]

foreach size : sizes
  install_data(
    'hi@0@-app-vibrismenu.png'.format(size),
    rename: 'org.xfce.panel.vibrismenu.png',
    install_dir: get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / '@0@x@0@'.format(size) / 'apps',
  )
endforeach

install_data(
  'hisc-app-vibrismenu.svg',
  rename: 'org.xfce.panel.vibrismenu.svg',
  install_dir: get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)
