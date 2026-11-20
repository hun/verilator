#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL exhaustive case over an integer subtype
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_case_int_subtype.vhdl'

# IEEE 1076-2008 10.9: a case statement's choices must cover the selector's
# subtype, not the base 32-bit integer type. GHDL accepts both lists below.

test.lint(verilator_flags2=vhdl_frontend.flags()
          + ['--language', 'vhdl', '--top-module', 't'])

test.passes()
