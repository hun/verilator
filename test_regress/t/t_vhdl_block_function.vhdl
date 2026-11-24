-- DESCRIPTION: Verilator: a function declared in a block or generate
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
  -- Shadowed inside the block and the generate by their own declarations
  function f(x : natural) return natural is
  begin
    return x;
  end function;
begin
  -- A constant of the block's declarative part calls the block's own function, which
  -- reads another constant declared before it
  blk : block is
    constant base : natural := 5;
    function f(x : natural) return natural is
    begin
      return base + x;
    end function;
    constant k : natural := f(2);
  begin
    o(0) <= '1' when k = 7 else '0';
  end block blk;

  -- A function of a generate reads the generate parameter
  gen : for i in 0 to 2 generate
    function f(x : natural) return natural is
    begin
      return x * 10 + i;
    end function;
    constant k : natural := f(2);
  begin
    o(1 + i) <= '1' when k = 20 + i else '0';
  end generate gen;

  -- Outside both, the architecture's own function is the visible one
  o(4) <= '1' when f(3) = 3 else '0';
  o(7 downto 5) <= "111";
end architecture rtl;
