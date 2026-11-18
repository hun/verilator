-- DESCRIPTION: Verilator: VHDL port actuals must conform to the formal
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- The actual's type must conform to the port's (IEEE 1076-2008 6.5.2.2);
-- GHDL refuses both instantiations below.

library ieee;
use ieee.std_logic_1164.all;

entity child is
  port (
    p : in  std_logic_vector(7 downto 0);
    y : out std_logic
  );
end;

architecture rtl of child is
begin
  y <= p(0);
end;

entity t is
  port (
    a4 : in  std_logic_vector(3 downto 0);
    b4 : in  bit_vector(3 downto 0);
    y  : out std_logic
  );
end;

architecture rtl of t is
  signal s8 : std_logic_vector(7 downto 0);
begin
  -- A width mismatch: a four-bit actual on an eight-bit port
  u1 : entity work.child port map (p => a4, y => y);
  -- An element-kind mismatch: a bit_vector actual on a std_logic_vector port
  u2 : entity work.child port map (p => b4, y => open);
end;
