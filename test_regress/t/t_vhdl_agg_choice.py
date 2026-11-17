#!/usr/bin/env python3
# DESCRIPTION: Verilator: an aggregate whose choice range names part of its bounds
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('simulator')
test.top_filename = 't/t_vhdl_agg_choice.vhdl'




test.compile(verilator_flags2=vhdl_frontend.flags() + ['--language', 'vhdl', '--binary'])

test.execute()

test.passes()
