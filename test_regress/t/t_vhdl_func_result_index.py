#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL one element of an array a function returns
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt_all')
test.top_filename = 't/t_vhdl_func_result_index.vhdl'

# The GHDL XML path selects a bit of the returned value instead of an element, so
# only the native frontend is checked here
vhdl_frontend.native_only(test)

test.compile(make_top_shell=False,
             make_main=False,
             verilator_flags2=[
                 *vhdl_frontend.flags(), '--language', 'vhdl', '--exe', test.pli_filename])

test.execute()

test.passes()
