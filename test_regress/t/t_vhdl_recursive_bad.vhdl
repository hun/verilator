-- DESCRIPTION: Verilator: a recursive VHDL function has no cycle model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_recursive is end entity;
architecture rtl of t_recursive is
  function f(n : natural) return natural is
  begin
    if n = 0 then
      return 0;
    end if;
    return f(n - 1);
  end function;
  constant c : natural := f(3);
begin
end architecture;
