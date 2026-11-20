-- DESCRIPTION: Verilator: VHDL a qualified array index subtype mark
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package qp2 is
  subtype idx is integer range 0 to 3;
end package;

use work.qp2.all;

entity tb is
end;

architecture sim of tb is
  type arr_t is array (work.qp2.idx) of integer;
  signal a : arr_t;
begin
  process
  begin
    assert a'length = 4 report "qualified index range not used" severity failure;
    std.env.finish;
    wait;
  end process;
end;
