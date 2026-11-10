#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL ports that cannot be modelled
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_port_bad.vhdl'

vhdl_frontend.require_frontend(test)

test.compile(verilator_flags2=vhdl_frontend.flags() + ['--language', 'vhdl', '--top-module', 'top_in'],
             fails=True,
             expect_filename=vhdl_frontend.golden_filename(test))

test.passes()
