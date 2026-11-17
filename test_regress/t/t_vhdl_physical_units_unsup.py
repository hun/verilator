#!/usr/bin/env python3
# DESCRIPTION: Verilator: a physical unit name two physical types give different values to
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_physical_units_unsup.vhdl'

# The native frontend refuses a physical literal of a user-declared type, so only the
# native message is pinned here


test.compile(verilator_flags2=vhdl_frontend.flags() + ['--language', 'vhdl', '--top-module', 't'],
             fails=True,
             expect_filename=test.golden_filename)

test.passes()
