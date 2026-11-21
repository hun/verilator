#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL signals, variables and ports are traced
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_trace.vhdl'

test.compile(make_top_shell=False,
             make_main=False,
             verilator_flags2=['--exe', '--trace-vcd', test.pli_filename])

test.execute()

# Ports and architecture signals are declared in the trace
test.file_grep(test.trace_filename, r'\$var wire +4 \S+ q ')
test.file_grep(test.trace_filename, r'\$var wire +4 \S+ count ')
test.file_grep(test.trace_filename, r'\$var wire +8 \S+ hist ')

test.passes()
