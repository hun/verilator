#!/usr/bin/env python3
# DESCRIPTION: Verilator: a call in a constant whose actual is not static is refused
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt')
test.top_filename = 't/t_vhdl_fold_nostatic_bad.vhdl'

# GHDL analyses the same source, so only the native frontend can pin the refusal
vhdl_frontend.native_only(test)

test.compile(verilator_flags2=vhdl_frontend.flags() + ['--language', 'vhdl', '--top-module', 't'],
             fails=True,
             expect_filename=vhdl_frontend.golden_filename(test))

test.passes()
