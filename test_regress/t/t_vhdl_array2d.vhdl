-- DESCRIPTION: Verilator: VHDL two-dimensional array index constraint
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- `array (0 to 1, 3 downto 0)` is a packed two-dimensional array; element access
-- is written both as a row slice `m(i)(3 downto 0)` and a scalar `m(i, j)`.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_array2d is
  port (
    a : in  std_logic_vector(7 downto 0);
    o : out std_logic_vector(3 downto 0);
    b : out std_logic
  );
end entity;

architecture a of t_vhdl_array2d is
  type t2 is array (0 to 1, 3 downto 0) of std_logic;
  signal m : t2;
begin
  m(0)(3 downto 0) <= a(7 downto 4);
  m(1)(3 downto 0) <= a(3 downto 0);
  o                <= m(1)(3 downto 0);
  b                <= m(0, 1);
end architecture;
