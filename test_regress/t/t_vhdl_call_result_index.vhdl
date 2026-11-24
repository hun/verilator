-- DESCRIPTION: Verilator: the result of a function without formals indexed in place
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out std_logic_vector(7 downto 0));
end entity t;

architecture rtl of t is
  type bits_t is array (natural range <>) of std_logic;
  type grid_t is array (natural range <>, natural range <>) of std_logic;

  function bits return bits_t is
    variable r : bits_t(0 to 3) := (others => '0');
  begin
    r(2) := '1';
    return r;
  end function;

  function grid return grid_t is
    variable r : grid_t(0 to 1, 0 to 2) := (others => (others => '0'));
  begin
    r(1, 2) := '1';
    r(0, 1) := '1';
    return r;
  end function;
begin
  -- An argument list after a function with no formals is an index into its result
  o(0) <= '1' when bits(2) = '1' else '0';
  o(1) <= '1' when bits(0) = '0' else '0';
  o(2) <= '1' when grid(1, 2) = '1' else '0';
  o(3) <= '1' when grid(0, 2) = '0' else '0';
  o(4) <= '1' when grid(0, 1) = '1' else '0';
  o(5) <= '1' when grid(1, 0) = '0' else '0';
  o(6) <= '1' when bits(3) = '0' else '0';
  o(7) <= '1' when bits(1) = '0' and grid(1, 1) = '0' else '0';
end architecture rtl;
