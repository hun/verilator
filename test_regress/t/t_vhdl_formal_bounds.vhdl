-- DESCRIPTION: Verilator: a constrained formal indexed by its own bounds
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i : in  std_logic_vector(31 downto 0);
    o : out std_logic_vector(31 downto 0)
  );
end entity t;

architecture rtl of t is
  function swap (input : std_logic_vector(7 downto 0)) return std_logic_vector is
  begin
    return input(3 downto 0) & input(7 downto 4);
  end function;

  function swap_all (input : in std_logic_vector(31 downto 0)) return std_logic_vector is
    variable v : std_logic_vector(31 downto 0);
  begin
    -- The slices of input carry bounds 31..24 and so on; swap still indexes 7 downto 0
    v := swap(input(31 downto 24)) & swap(input(23 downto 16)) & swap(input(15 downto 8))
         & swap(input(7 downto 0));
    return v;
  end function;
begin
  o <= swap_all(i);
end architecture rtl;
