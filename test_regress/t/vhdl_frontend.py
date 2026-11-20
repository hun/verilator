#!/usr/bin/env python3
# DESCRIPTION: Verilator: Shared setup for the VHDL tests
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

"""Flags shared by the VHDL regression tests.

The in-tree frontend is the only VHDL frontend. It needs no external analyzer,
so the tests pass no frontend selector.
"""


def flags():
    """Frontend-related Verilator flags for a VHDL test."""
    return []
