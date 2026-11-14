-- DESCRIPTION: Verilator: VHDL subtype of another package's unconstrained array type
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package pkg_a is
  type nat_vec_t is array (integer range <>) of natural;
end package pkg_a;

library work;
use work.pkg_a.all;

package pkg_b is
  -- The shape of `non_zero_taps_t` in hdl-modules lfsr_pkg.vhd: a subtype of an
  -- unconstrained array type that another package declares
  subtype taps_t is nat_vec_t(0 to 4);
  type table_t is array (2 to 3) of taps_t;
  constant table : table_t := (2 => (1, 2, 3, 4, 5), 3 => (6, 7, 8, 9, 10));
end package pkg_b;

library ieee;
use ieee.std_logic_1164.all;
library work;
use work.pkg_b.all;

entity t is
  port (
    i_a     : in  std_ulogic;
    o_first : out integer;
    o_last  : out integer;
    o_len   : out integer
  );
end entity t;

architecture rtl of t is
  signal taps : taps_t := (9, 8, 7, 6, 5);
begin
  o_first <= integer(table(2)(0)) when i_a = '1' else integer(table(3)(0));
  o_last  <= integer(table(2)(4)) when i_a = '1' else integer(table(3)(4));
  o_len   <= taps_t'length + taps(1);
end architecture rtl;
