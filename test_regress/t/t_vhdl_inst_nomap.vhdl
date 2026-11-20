-- DESCRIPTION: Verilator: VHDL a component instantiation with no maps
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity leaf is
end;

architecture rtl of leaf is
begin
end;

entity tb is
end;

architecture sim of tb is
  component leaf is
  end component;
begin
  u : leaf;
  process
  begin
    wait for 1 ns;
    std.env.finish;
    wait;
  end process;
end;
