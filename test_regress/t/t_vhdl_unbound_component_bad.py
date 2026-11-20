#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL component bound to a non-VHDL unit
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_unbound_component_bad.vhdl'



# The SystemVerilog module of that name is in this invocation too: a VHDL instance still
# binds only to entities analyzed with it, so the design is refused with the same message
test.compile(verilator_flags2=vhdl_frontend.flags()
             + ['--language', 'vhdl', 't/t_vhdl_unbound_component_bad.v'],
             fails=True,
             expect_filename=test.golden_filename)

test.passes()
