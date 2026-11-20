-- DESCRIPTION: Verilator: an empty string as a value
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A string generic bound to "" (or defaulted to it) is the null string `1 to 0`: it has
-- no characters, but its bounds and length are values the design reads, and functions
-- that take it loop over a null range. A character literal compared inside such a
-- function is a character code, not a logic value.

library ieee;
use ieee.std_logic_1164.all;

package sp is
  function commas(a : string) return natural;
  function firstNonSpace(a : string) return natural;
  function spaceCode return integer;
end package;

package body sp is
  function commas(a : string) return natural is
    variable count_v : natural := 0;
  begin
    for i in a'range loop
      if a(i) = ',' then
        count_v := count_v + 1;
      end if;
    end loop;
    return count_v;
  end function;

  function firstNonSpace(a : string) return natural is
  begin
    for i in a'range loop
      if a(i) /= ' ' then
        return i;
      end if;
    end loop;
    return 0;
  end function;

  function spaceCode return integer is
  begin
    return character'pos(' ');
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.sp.all;

entity nul_str is
  generic (S : string := "");
  port (
    n : out natural;
    m : out integer;
    p : out integer
  );
end entity;

architecture rtl of nul_str is
  constant Len_c : natural := S'length;
  constant Cnt_c : natural := commas(S);
  constant Lft_c : natural := S'left;
  constant Rgt_c : natural := S'right;
begin
  n <= Len_c + Cnt_c * 10 + Lft_c * 100 + Rgt_c * 1000;
  m <= firstNonSpace(S);
  p <= spaceCode;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.sp.all;

entity t is
  port (
    n1 : out natural;
    m1 : out integer;
    p1 : out integer;
    n2 : out natural;
    m2 : out integer;
    p2 : out integer
  );
end entity;

architecture rtl of t is
begin
  u_null : entity work.nul_str
    port map (n => n1, m => m1, p => p1);

  u_set : entity work.nul_str
    generic map (S => "a,b,c")
    port map (n => n2, m => m2, p => p2);
end architecture;
