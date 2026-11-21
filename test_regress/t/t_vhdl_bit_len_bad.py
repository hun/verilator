#!/usr/bin/env python3
# DESCRIPTION: Verilator: a bit-string literal whose length truncates its value
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_bit_len_bad.vhdl'

# GHDL reports the same rule in its own words; the golden pins the frontend message


test.lint(verilator_flags2=['--language', 'vhdl', '--top-module', 't'],
             fails=True,
             expect_filename=test.golden_filename)

test.passes()
