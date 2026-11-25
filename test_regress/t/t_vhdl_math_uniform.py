#!/usr/bin/env python3
# DESCRIPTION: Verilator: uniform of ieee.math_real follows the IEEE 1076.2 generator
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('simulator')
test.top_filename = 't/t_vhdl_math_uniform.vhdl'

test.compile(verilator_flags2=['--language', 'vhdl', '--binary'])

test.execute()

test.file_grep(test.run_log_filename, r'all uniform values match')

test.passes()
