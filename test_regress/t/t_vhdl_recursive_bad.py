#!/usr/bin/env python3
# DESCRIPTION: Verilator: a recursive VHDL function has no cycle model
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_recursive_bad.vhdl'



test.compile(verilator_flags2=vhdl_frontend.flags() + ['--language', 'vhdl', '--top-module', 't_recursive'],
             fails=True,
             expect_filename=test.golden_filename)

test.passes()
