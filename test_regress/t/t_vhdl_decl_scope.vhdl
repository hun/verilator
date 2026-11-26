-- DESCRIPTION: Verilator: a subprogram sees the declarative items declared before it
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- poc's arith_Adder_Wide declares a constant table and a function that reads it in the
-- same declarative part, and a constant that calls the function. A subprogram body
-- evaluates against what its unit has declared before it (IEEE 1076-2008 14.4.1).

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    clk : in  std_logic;
    o   : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t is
  constant table_c : integer_vector := (10, 11, 12);

  function pick(i : integer) return integer is
  begin
    return table_c(i);
  end function;

  constant chosen_c : integer := pick(1);
begin
  process (clk)
  begin
    if rising_edge(clk) then
      o <= std_logic_vector(to_unsigned(chosen_c, 8));
    end if;
  end process;
end architecture;
