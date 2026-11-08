#!/usr/bin/env python3
# DESCRIPTION: Verilator: malformed VHDL through GHDL XML
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import os
import shutil

import vltest_bootstrap

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_ghdl_bad.vhdl'

ghdl = os.environ.get('VERILATOR_GHDL', 'ghdl')
if not shutil.which(ghdl):
    test.skip("GHDL not available")

test.compile(verilator_flags2=['--language', 'vhdl'],
             fails=True,
             expect_filename=test.golden_filename)

test.passes()
