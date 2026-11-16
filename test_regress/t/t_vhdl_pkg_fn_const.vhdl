-- DESCRIPTION: Verilator: VHDL package constant initialized by a package function
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package calc_pkg is
  function widen(i : natural) return natural;
end package calc_pkg;

package body calc_pkg is
  function widen(i : natural) return natural is
  begin
    return i * 2;
  end function widen;
end package body calc_pkg;

library work;
use work.calc_pkg.all;

package size_pkg is
  constant base_c : natural := 4;
  -- The call needs calc_pkg's body, which is read after the specifications
  constant wide_c : natural := widen(base_c);
  -- A type that names the constant cannot be registered until it has a value
  subtype wide_t is bit_vector(wide_c - 1 downto 0);
end package size_pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.size_pkg.all;

entity t is
  port (
    o : out std_logic_vector(7 downto 0);
    l : out integer
  );
end entity t;

architecture rtl of t is
  signal s : wide_t;
begin
  o <= std_logic_vector(to_unsigned(wide_c, 8));
  l <= wide_t'length;
  s <= (others => '1');
end architecture rtl;
