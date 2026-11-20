#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL port actuals must conform to the formal
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_port_conform_bad.vhdl'

# IEEE 1076-2008 6.5.2.2: the actual must conform to the port's type

test.compile(verilator_flags2=['--language', 'vhdl', '--top-module', 't'],
             fails=True,
             expect_filename=test.golden_filename)

test.passes()
