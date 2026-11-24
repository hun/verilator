-- DESCRIPTION: Verilator: a function called without parentheses takes the target's length
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
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(7 downto 0)
  );
end entity t;

architecture rtl of t is
  constant Len_c : integer := 8;
  signal reg : std_logic_vector(Len_c-1 downto 0) := (others => '0');

  -- The result is unconstrained, so its length comes from the target
  function init_value return std_logic_vector is
    variable v : std_logic_vector(Len_c-1 downto 0) := (others => '0');
  begin
    v(3 downto 0) := "1010";
    return v;
  end function init_value;
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        reg <= init_value;
      end if;
    end if;
  end process;
  q <= reg;
end architecture rtl;
