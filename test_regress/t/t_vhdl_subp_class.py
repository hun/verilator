#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL overloads across subprogram classes resolve by context
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt_all')
test.top_filename = 't/t_vhdl_subp_class.vhdl'

test.compile(verilator_flags2=[
    '--language', 'vhdl', '--top-module', 'tb', '--binary'
])

test.execute()

test.passes()
