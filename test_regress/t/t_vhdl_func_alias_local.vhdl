-- DESCRIPTION: Verilator: VHDL an alias of a local constant in a function
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_func_alias_local is
  port (sel : in  std_logic_vector(1 downto 0);
        q   : out std_logic_vector(3 downto 0));
end t_vhdl_func_alias_local;

architecture behav of t_vhdl_func_alias_local is
  function pick (s : natural) return std_logic_vector is
    constant b : std_logic_vector (1 to 4) := "0011";
    alias same : std_logic_vector (1 to 4) is b;
    -- Another alias renames the first with different bounds
    alias twisted : std_logic_vector (11 to 14) is same;
    variable r : std_logic_vector (3 downto 0);
  begin
    r := (3 => same (1), 2 => same (3), 1 => twisted (11 + s mod 4), 0 => twisted (14));
    return r;
  end function;
begin
  q <= pick (3) when sel = "11" else pick (0);
end behav;
