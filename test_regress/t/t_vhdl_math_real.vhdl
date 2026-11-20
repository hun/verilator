-- DESCRIPTION: Verilator: VHDL locally static math_real expressions
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

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
