#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL assignment, return, call, generic and conversion type conformance
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_type_conf_bad.vhdl'

# IEEE 1076-2008 10.5.1, 10.5.2, 12.5 and 9.3.6: assignment, return, call and
# conversion values must belong to the target's base type or be closely related

test.compile(verilator_flags2=vhdl_frontend.flags() + ['--language', 'vhdl', '--top-module', 't'],
             fails=True,
             expect_filename=test.golden_filename)

test.passes()
