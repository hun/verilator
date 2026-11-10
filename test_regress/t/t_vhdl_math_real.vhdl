-- DESCRIPTION: Verilator: VHDL locally static math_real expressions
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.math_real.all;

entity t is
  generic (
    g : positive := 100
  );
  port (
    ceil_o  : out integer;
    floor_o : out integer;
    len_o   : out integer;
    pow_o   : out integer
  );
end entity t;

architecture rtl of t is
  -- Locally static math_real folding in a constant and in a subtype range
  constant width_c : natural := natural(ceil(log2(real(g))));
  constant floor_c : natural := natural(floor(log2(real(g))));
  constant pow_c   : natural := natural(ceil(log2(real(2 ** 6))));
  subtype word_t is std_ulogic_vector(width_c - 1 downto 0);
  signal word : word_t;
begin
  ceil_o  <= width_c;
  floor_o <= floor_c;
  len_o   <= word'length;
  pow_o   <= pow_c;
  word    <= (others => '0');
end architecture rtl;
