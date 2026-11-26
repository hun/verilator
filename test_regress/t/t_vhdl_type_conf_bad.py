#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL assignment, return, call, generic and conversion type conformance
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_type_conf_bad.vhdl'

# IEEE 1076-2008 10.5.2.1 (assignment), 10.13 (return), 6.5.7.1 (call) and 9.3.6
# (conversion): the values must belong to the target's base type or be closely related

test.lint(verilator_flags2=['--language', 'vhdl', '--top-module', 't'],
          fails=True,
          expect_filename=test.golden_filename)

test.passes()
