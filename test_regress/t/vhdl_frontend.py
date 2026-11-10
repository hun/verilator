#!/usr/bin/env python3
# DESCRIPTION: Verilator: Shared VHDL frontend selection for the VHDL tests
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

"""Selects the VHDL frontend under test for every t_vhdl_*.py test.

VERILATOR_TEST_VHDL_FRONTEND picks 'ghdl' (default) or 'native'. The tests then
pass the matching --vhdl-frontend flag, skip when the selected frontend is not
available, and compare against the matching golden file.
"""

import os
import re
import shutil

FRONTEND = os.environ.get('VERILATOR_TEST_VHDL_FRONTEND', 'ghdl')


def flags():
    """Verilator flags selecting the frontend under test."""
    return ['--vhdl-frontend', FRONTEND]


def require_frontend(test):
    """Skip unless the selected frontend can run this test."""
    if FRONTEND == 'native':
        return
    ghdl = os.environ.get('VERILATOR_GHDL', 'ghdl')
    if not shutil.which(ghdl):
        test.skip("GHDL not available")


def ghdl_only(test):
    """Skip when the test exercises a GHDL-specific behavior."""
    if FRONTEND == 'native':
        test.skip("GHDL-specific test")


def golden_filename(test):
    """Golden file name for the selected frontend."""
    if FRONTEND == 'native':
        return re.sub(r'\.out$', '.native.out', test.golden_filename)
    return test.golden_filename
