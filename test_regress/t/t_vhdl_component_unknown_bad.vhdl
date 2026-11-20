-- DESCRIPTION: Verilator: VHDL component association names a declared port
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity leaf is
  port (
    a : in  std_logic;
    b : out integer
  );
end;

architecture rtl of leaf is
begin
  b <= 1;
end;

entity t is
  port (
    i : in  std_logic;
    o : out integer
  );
end;

architecture rtl of t is
  component leaf is
    port (
      a : in  std_logic;
      b : out integer
    );
  end component;
begin
  u : leaf port map (a => i, c => o);
end;
