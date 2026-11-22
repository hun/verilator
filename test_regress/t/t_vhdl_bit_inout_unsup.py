#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL two-state inout port diagnostic
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_bit_inout_unsup.vhdl'

# A `bit` inout port is legal VHDL but not representable as a V3Tristate net;
# the frontend must report it instead of leaking into the net-type check.

test.lint(verilator_flags2=['--language', 'vhdl', '--top-module', 't'],
          fails=True,
          expect_filename=test.golden_filename)

test.passes()
