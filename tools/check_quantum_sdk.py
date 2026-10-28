#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Standalone quantum proof against published RC1 or an explicitly locked development SDK.

Same machinery as check_teleport_sdk.py. The actual-module workflow drives the
player switch, the game.relocation capability, one request and its tick tracking
with synthetic providers; the SDK checker has none, so travel answers unavailable.
POSIX red arm: the module workflow must fail without the player switch.
"""
import sys

sys.dont_write_bytecode = True
from check_teleport_sdk import main

QUANTUM = {
    'feature': 'quantum',
    'test_option': 'SCO_QUANTUM_TEST', 'test_target': 'quantum-workflow-test', 'test_name': 'quantum-workflow',
    'test_source': 'quantum_workflow_test.cpp',
    'extra_tests': [('quantum_content_test.cpp', 'quantum-content-test', 'quantum-content'),
                    ('quantum_gamerules_test.cpp', 'quantum-gamerules-test', 'quantum-gamerules')],
    'checks': [
        # quantum.travel takes arguments, so it is invoked explicitly.
        (['--invoke', 'quantum.travel', 'Port', '500'],
         ('setting relocation (bool)', 'quantum.travel -> unavailable', 'game.relocation is off',
          'quantum.scan -> unavailable', 'quantum.status -> ok', 'game.relocation off', 'OK: 0 failure(s)')),
    ],
    'policy_red_arm': ('plugins/quantum/controls.h',
                       'return g.rules_known && g.rules.quantum_travel_allowed && g.rules.quantum_boost_allowed;',
                       'return true;', 'quantum-gamerules', 'unknown policy pauses new boost UI'),
    'red_arm': ('plugins/spatial/travel_plugin.h',
                '    if (!MovesAllowed()) { why=switched_off; return SCO_UNAVAILABLE; }\n', ''),
}

if __name__ == '__main__':
    main(spec=QUANTUM)
