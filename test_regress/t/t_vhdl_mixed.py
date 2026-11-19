#!/usr/bin/env python3
# DESCRIPTION: Verilator: SystemVerilog and VHDL sources elaborate in one invocation
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt_all')
test.top_filename = 't/t_vhdl_mixed.v'

# One invocation: the SystemVerilog top instantiates the entity of the .vhdl file by name
test.compile(make_top_shell=False,
             make_main=False,
             verilator_flags2=['--exe', test.pli_filename, 't/t_vhdl_mixed.vhdl'])

test.execute()

test.passes()
