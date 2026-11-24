-- DESCRIPTION: Verilator: the length of a slice as a conversion width
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;

entity t is
  generic (dw : integer := 8);
  port (a : in std_logic_vector(8 downto 0);
        c : in std_logic;
        o : out std_logic_vector(8 downto 0));
end entity t;

architecture rtl of t is
begin
  process (a, c)
    variable v : unsigned(dw downto 0);
    variable cy : std_logic;
  begin
    v := unsigned(a);
    cy := c;
    -- The width is the length of a slice whose bounds are static in each iteration
    for j in 0 to 1 loop
      v(dw downto j * 4) := v(dw downto j * 4) + conv_unsigned(cy, v(dw downto j * 4)'length);
    end loop;
    o <= std_logic_vector(v);
  end process;
end architecture rtl;
