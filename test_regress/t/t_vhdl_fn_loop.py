#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL folded call in a slice bound and exponent
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt_all')
test.top_filename = 't/t_vhdl_fn_loop.vhdl'

# The GHDL path declines a call whose body loops, so only the native fold is exercised
vhdl_frontend.native_only(test)

test.compile(make_top_shell=False,
             make_main=False,
             verilator_flags2=[
                 *vhdl_frontend.flags(), '--language', 'vhdl', '--exe', test.pli_filename])

test.execute()

test.passes()
