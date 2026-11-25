-- DESCRIPTION: Verilator: a function local of a million elements
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Each element write of an expression-bound local would copy the whole value, which is
-- more than the frontend allows; as a statement it is one store

library ieee;
use ieee.std_logic_1164.all;

entity t is end entity;

architecture rtl of t is
  function fill (n : natural; b : std_logic) return std_logic is
    variable bits : std_logic_vector(1048575 downto 0);
  begin
    for i in 0 to 19 loop
      bits(i) := b;
    end loop;
    return bits(n);
  end function;
  signal d : std_logic := '1';
  signal o : std_logic;
begin
  o <= fill(3, d);
  process
  begin
    wait for 1 ns;
    assert o = '1' report "d = 1" severity failure;
    d <= '0';
    wait for 1 ns;
    assert o = '0' report "d = 0" severity failure;
    std.env.finish;
    wait;
  end process;
end architecture;
