// DESCRIPTION: Verilator: SystemVerilog top instantiating a VHDL entity in one invocation
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

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
