#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL folded calls where a width is expected
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt_all')
test.top_filename = 't/t_vhdl_folded_width.vhdl'



test.compile(make_top_shell=False,
             make_main=False,
             verilator_flags2=[
                 '--language', 'vhdl', '--exe', test.pli_filename])

test.execute()

test.passes()
