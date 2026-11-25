#!/usr/bin/env python3
# DESCRIPTION: Verilator: real elements and record fields of constant aggregates
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('simulator')
test.top_filename = 't/t_vhdl_real_agg.vhdl'

test.compile(verilator_flags2=['--language', 'vhdl', '--binary'])

test.execute()

test.file_grep(test.run_log_filename, r'all real aggregates match')

test.passes()
