#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL committed AST tree golden for one entity
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt_all')
test.top_filename = 't/t_vhdl_cnt.vhdl'

test.lint(v_flags=['--language vhdl --dump-tree --dump-tree-addrids'])

out_filename = test.glob_one(test.obj_dir + '/Vt_*_cellsort.tree')
test.files_identical(out_filename, test.golden_filename)

test.passes()
