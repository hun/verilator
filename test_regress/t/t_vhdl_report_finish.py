#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL computed report with severity failure ends the run
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt_all')
test.top_filename = 't/t_vhdl_report_finish.vhdl'

test.compile(make_top_shell=False,
             make_main=False,
             verilator_flags2=[
                 *vhdl_frontend.flags(), '--language', 'vhdl', '--exe', test.pli_filename])

test.execute()

test.passes()
