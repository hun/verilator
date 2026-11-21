-- DESCRIPTION: Verilator: VHDL real compared with integer diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A real never compares with an integer, also where the comparison folds statically.

entity top is
  port (
    clk : in bit;
    o   : out bit;
    p   : out bit
  );
end entity;

architecture rtl of top is
  constant r : real := 1.5;
  constant i : integer := 2;
begin
  o <= '1' when r < i else '0';
  process (clk)
  begin
    if r > i then
      p <= '1';
    end if;
  end process;
end architecture;
