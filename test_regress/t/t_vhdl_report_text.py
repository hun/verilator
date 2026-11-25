#!/usr/bin/env python3
# DESCRIPTION: Verilator: a report message built from literals and integer images
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('simulator')
test.top_filename = 't/t_vhdl_report_text.vhdl'

test.compile(verilator_flags2=['--language', 'vhdl', '--binary'])

test.execute(expect=r'plain 100% done\ncount 42 and 8!\nchar x paren -5\nn was 42')

test.passes()
