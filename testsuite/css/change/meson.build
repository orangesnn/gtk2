testexecdir = join_paths(installed_test_bindir, 'css', 'change')
testdatadir = join_paths(installed_test_datadir, 'css')

test_change = executable(
  'test-css-change',
  'test-css-change.c',
  c_args: common_cflags,
  dependencies: libgtk_dep,
  install: get_option('install-tests'),
  install_dir: testexecdir,
)

test('change', test_change,
  args: [ '--tap', '-k' ],
  protocol: 'tap',
  env: csstest_env,
  suite: 'css',
)

test_data = [
  'test1.css', 'test1.ui', 'test1.nodes',
  'test2.css', 'test2.ui', 'test2.nodes',
  'test3.css', 'test3.ui', 'test3.nodes',
  'test4.css', 'test4.ui', 'test4.nodes',
]

if get_option('install-tests')
  conf = configuration_data()
  conf.set('libexecdir', gtk_libexecdir)
  configure_file(input: 'change.test.in',
                 output: 'change.test',
                 configuration: conf,
                 install_dir: testdatadir)

  install_data(test_data, install_dir: testexecdir)

endif
