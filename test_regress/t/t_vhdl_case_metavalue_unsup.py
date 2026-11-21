#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL metavalue choice diagnostic
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_case_metavalue_unsup.vhdl'

# A metavalue choice is legal VHDL but has no two-state ordinal; the frontend
# must refuse it instead of asserting in V3Number::toUInt.

test.lint(verilator_flags2=['--language', 'vhdl', '--top-module', 't'],
             fails=True,
             expect_filename=test.golden_filename)

test.passes()
