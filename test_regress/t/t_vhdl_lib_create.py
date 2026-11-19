#!/usr/bin/env python3
# DESCRIPTION: Verilator: VHDL hierarchy built with --lib-create and driven from SystemVerilog
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('vlt_all')

# First run: Verilate and build the VHDL design as a DPI library
lib_dir = test.obj_dir + "/sub"
test.mkdir_ok(lib_dir)

test.run(logfile=lib_dir + "/verilator.log",
         cmd=[
             "perl", os.environ["VERILATOR_ROOT"] + "/bin/verilator", "-cc", "-Mdir", lib_dir,
             "--prefix", "Vvhdlsub", "--lib-create", "vhdlsub", "--top-module", "dut",
             "--language", "vhdl", "--debug-check", "t/t_vhdl_lib_create.vhdl"
         ],
         verilator_run=True)

test.run(logfile=lib_dir + "/make.log",
         cmd=[os.environ["MAKE"], "-C", lib_dir, "-f", "Vvhdlsub.mk"])

# Second run: SystemVerilog top instantiating the generated wrapper module
test.top_filename = 't/t_vhdl_lib_create.v'
test.compile(verilator_flags2=["--binary", "-LDFLAGS", "sub/libvhdlsub.a", lib_dir + "/vhdlsub.sv"])

test.execute()

test.passes()
