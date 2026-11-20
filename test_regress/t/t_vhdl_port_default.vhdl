-- DESCRIPTION: Verilator: VHDL an unassociated input takes its port default
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity leaf is
  port (
    a : in  std_logic_vector(3 downto 0) := "1010";
    b : out integer
  );
end;

architecture rtl of leaf is
begin
  b <= to_integer(unsigned(a));
end;

entity tb is
end;

architecture sim of tb is
  signal y1 : integer := 0;
  signal y2 : integer := 0;
begin
  -- One instance omits the defaulted input, the other leaves it open
  u1 : entity work.leaf port map (b => y1);
  u2 : entity work.leaf port map (a => open, b => y2);
  process
  begin
    wait for 1 ns;
    assert y1 = 10 report "omitted default not used" severity failure;
    assert y2 = 10 report "open default not used" severity failure;
    std.env.finish;
    wait;
  end process;
end;
