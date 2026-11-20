-- DESCRIPTION: Verilator: an array local of an inlined function
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An element write to an array local rebuilds the whole value, and every read of the
-- local copies that value into the rebuild, so a loop that reads the local back would
-- copy it a factor per write. The local is a real variable instead, and its element
-- writes are statements.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package util is
  function twice(a : natural) return natural;
end package;

package body util is
  function twice(a : natural) return natural is
  begin
    return 2 * a;
  end function;
end package body;

package p is
  function heavy(v : unsigned) return unsigned;
end package;

package body p is
  use work.util.all;

  function heavy(v : unsigned) return unsigned is
    -- The loop bound folds through a call, and the array local is a variable rather
    -- than a value rebuilt per element write
    constant w : natural := twice(16);
    type arr_t is array (natural range <>) of unsigned(31 downto 0);
    variable a : arr_t(0 to 1) := (others => (others => '0'));
  begin
    a(0) := resize(v, 32);
    for i in 0 to w - 1 loop
      a(1)(i) := a(0)(i) or a(0)((i / 2 + 1) mod 32);
      a(0)(i) := a(1)(i) xor a(0)((i + 3) mod 32);
    end loop;
    return a(1);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.p.all;

entity t is
  port (i : in unsigned(7 downto 0); o : out unsigned(31 downto 0));
end entity;

architecture rtl of t is
begin
  o <= heavy(i);
end architecture;
