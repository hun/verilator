-- DESCRIPTION: Verilator: a string formal's default value takes the string type in a call
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    q : out std_logic_vector(7 downto 0)
  );
end entity t;

architecture rtl of t is
  function len(s : string) return natural is
  begin
    return s'length;
  end function len;

  -- The default of b is used by the call below, and passed on to len
  function wrap(a : string; b : string := "hello") return natural is
  begin
    return len(a) * 10 + len(b);
  end function wrap;

  constant c_c : natural := wrap("ab");
begin
  q <= std_logic_vector(to_unsigned(c_c, 8));
end architecture rtl;
