#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL based literal base diagnostic
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_lex_base_bad.vhdl'

# A based literal with a base outside 2 to 16 is rejected.

test.lint(verilator_flags2=['--language', 'vhdl'],
          fails=True,
          expect_filename=test.golden_filename)

test.passes()
