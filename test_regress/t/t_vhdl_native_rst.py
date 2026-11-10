#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL async reset through the in-tree native frontend
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import os

import vltest_bootstrap

test.scenarios('vlt_all')
test.top_filename = 't/t_vhdl_native_rst.vhdl'

os.environ['VERILATOR_VHDL_FRONTEND'] = 'native'
os.environ['VERILATOR_GHDL'] = '/nonexistent/ghdl'

test.compile(make_top_shell=False,
             make_main=False,
             verilator_flags2=['--language', 'vhdl', '--exe', test.pli_filename])

test.execute()

test.passes()
