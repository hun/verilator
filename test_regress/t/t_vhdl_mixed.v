// DESCRIPTION: Verilator: SystemVerilog top instantiating a VHDL entity in one invocation
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

// The VHDL entity of t_vhdl_mixed.vhdl is instantiated by name, in the same invocation
module top (
    input logic clk,
    input logic en,
    output logic [3:0] q
);

  mixed_dut dut (
      .clk(clk),
      .en(en),
      .q(q)
  );

endmodule
