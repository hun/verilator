-- DESCRIPTION: Verilator: VHDL folded calls where a width is expected
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package width_pkg is
  function lg(i : natural) return natural;
end package width_pkg;

package body width_pkg is
  function lg(i : natural) return natural is
    variable ret : natural := 0;
    variable tmp : natural := i;
  begin
    while tmp > 1 loop
      ret := ret + 1;
      tmp := tmp / 2;
    end loop;
    return ret;
  end function lg;
end package body width_pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.width_pkg.all;

entity t is
  port (
    o1 : out std_logic_vector(7 downto 0);
    o2 : out std_logic_vector(2 downto 0)
  );
end entity t;

architecture rtl of t is
  constant size_c : natural := 8;
begin
  -- An aggregate whose bounds are a folded call, as a concatenation operand
  o1 <= (lg(size_c) + 1 downto 0 => '0') & b"101";
  -- A conversion whose width is a folded call
  o2 <= std_logic_vector(to_unsigned(5, lg(size_c)));
end architecture rtl;
