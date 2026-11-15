#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL a case that covers every value of its selector
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt_all')

# The GHDL XML path wants a case to be exhaustive in writing, so only the native
# frontend is checked here
vhdl_frontend.native_only(test)
test.top_filename = 't/t_vhdl_case_cover.vhdl'


test.compile(make_top_shell=False,
             make_main=False,
             verilator_flags2=[
                 *vhdl_frontend.flags(), '--language', 'vhdl', '--exe', test.pli_filename])

test.execute()

test.passes()