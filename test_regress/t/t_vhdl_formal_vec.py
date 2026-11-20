#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL subprogram formal of an unconstrained array type
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt_all')
test.top_filename = 't/t_vhdl_formal_vec.vhdl'

# The native frontend indexes only vectors, so it refuses an element of an array formal, an
# element assignment in an inlined function, and a function whose body loops




test.compile(make_top_shell=False,
             make_main=False,
             verilator_flags2=[
                 '--language', 'vhdl', '--exe', test.pli_filename])

test.execute()

test.passes()
