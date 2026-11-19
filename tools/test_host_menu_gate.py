#!/usr/bin/env python3
"""Exercise the product CMake host-menu option and its dependency gate."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
source = (root / 'CMakeLists.txt').read_text()
option = '\n'.join(line for line in source.splitlines()
                   if line.startswith(('option(SCO_HOST_MENU_CLIENT ', 'option(SCO_BUILD_RUST_MENU_HOST ')))
assert 'option(SCO_BUILD_RUST_MENU_HOST ' in option and 'option(SCO_HOST_MENU_CLIENT ' in option
start = source.index('if(SCO_HOST_MENU_CLIENT OR SCO_BUILD_RUST_MENU_HOST)\n  if(NOT TARGET sco_widgets_imgui')
end = source.index('function(sco_add_host_dll', start)
gate = source[start:end]
assert 'if(menu_client AND EXISTS "${SCO_CORE_SOURCE_DIR}/include/sco/host_menu.h")' in source
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'include/sco').mkdir(parents=True)
    (path / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.30)\nproject(gate NONE)\n' + option + '\n'
        'set(SCO_CORE_SOURCE_DIR "${CMAKE_SOURCE_DIR}")\n'
        'if(WITH_RENDERER)\n add_library(sco_widgets_imgui INTERFACE)\nendif()\n' + gate
        + '\nfile(WRITE "${CMAKE_BINARY_DIR}/enabled" "${SCO_HOST_MENU_CLIENT}")\n')
    def configure(name, *args, ok):
        run = subprocess.run(['cmake', '-S', str(path), '-B', str(path/name), *args], capture_output=True, text=True)
        assert (run.returncode == 0) == ok, run.stdout + run.stderr
        return run
    configure('default', ok=True)
    assert (path/'default/enabled').read_text() == 'OFF'
    configure('no_renderer', '-DSCO_HOST_MENU_CLIENT=ON', ok=False)
    configure('no_header', '-DSCO_HOST_MENU_CLIENT=ON', '-DWITH_RENDERER=ON', ok=False)
    (path/'include/sco/host_menu.h').write_text('')
    (path/'include/sco_widgets.h').write_text('')
    configure('no_v13', '-DSCO_HOST_MENU_CLIENT=ON', '-DWITH_RENDERER=ON', ok=False)
    configure('rust_host_no_v13', '-DSCO_BUILD_RUST_MENU_HOST=ON', '-DWITH_RENDERER=ON', ok=False)
    (path/'include/sco_widgets.h').write_text('#define SCO_WIDGETS_VERSION_1_3 3\n')
    configure('enabled', '-DSCO_HOST_MENU_CLIENT=ON', '-DWITH_RENDERER=ON', ok=True)
    assert (path/'enabled/enabled').read_text() == 'ON'
    # The second game DLL asks for the same prerequisites, but leaves the first DLL's option OFF.
    configure('rust_host', '-DSCO_BUILD_RUST_MENU_HOST=ON', '-DWITH_RENDERER=ON', ok=True)
    assert (path/'rust_host/enabled').read_text() == 'OFF'
print('host menu gate: default off, missing prerequisites refused, explicit opt-in accepted')
