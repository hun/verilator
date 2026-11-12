-- DESCRIPTION: Verilator: VHDL mixed signed/unsigned and int/vector operands
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A typeless bit-string literal adopts the signedness of the operand it is
-- compared with, and an integer matches a vector by taking on its type.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_mix is
  generic (INT : integer := 5);
  port (
    v   : in  std_logic_vector(2 downto 0);
    sge : out std_logic;  -- (-signed(v)) >= "110"  ("110" taken as signed, = -2)
    ueq : out std_logic;  -- unsigned(v) ?= INT
    sne : out std_logic   -- signed(v) ?/= INT
  );
end entity;

architecture a of t_vhdl_mix is
  signal vs : signed(2 downto 0);
  signal vu : unsigned(2 downto 0);
begin
  vs  <= signed(v);
  vu  <= unsigned(v);
  sge <= '1' when -vs >= "110" else '0';
  ueq <= '1' when vu  ?=  INT  else '0';
  sne <= '1' when vs  ?/= INT  else '0';
end architecture;
