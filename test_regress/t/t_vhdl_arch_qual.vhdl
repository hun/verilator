-- DESCRIPTION: Verilator: VHDL a qualified architecture entity name
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity e is
  port (y : out integer);
end;

architecture rtl of e is
begin
  y <= 1;
end;

architecture alt of work.e is
begin
  y <= 2;
end;

entity tb is
end;

architecture sim of tb is
  signal y : integer := 0;
begin
  u : entity work.e(alt) port map (y => y);
  process
  begin
    wait for 1 ns;
    assert y = 2 report "qualified architecture not used" severity failure;
    std.env.finish;
    wait;
  end process;
end;
