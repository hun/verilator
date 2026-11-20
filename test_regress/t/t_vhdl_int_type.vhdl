-- DESCRIPTION: Verilator: VHDL an integer type definition
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity tb is
end;

architecture sim of tb is
  type small_t is range 0 to 7;
  signal s : small_t := 3;
begin
  process
  begin
    wait for 1 ns;
    assert s = 3 report "integer type value wrong" severity failure;
    std.env.finish;
    wait;
  end process;
end;
