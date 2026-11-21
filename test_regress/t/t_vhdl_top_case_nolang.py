#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL a mixed-case --top-module name without --language
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_top_case.vhdl'

# The .vhdl extension alone selects VHDL, which still folds the --top-module name
test.lint(verilator_flags2=['--top-module', 'CaseTop'])

test.passes()
