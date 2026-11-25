-- DESCRIPTION: Verilator: uniform of ieee.math_real follows the IEEE 1076.2 generator
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The expected values were produced by GHDL from the same seeds: each row is the
-- seed1, seed2 after the call and the result scaled by 1e9 and rounded.

library ieee;
use ieee.math_real.all;

entity t is
end entity;

architecture test of t is
  type row_t is array (0 to 2) of integer;
  type table_t is array (natural range <>) of row_t;
  constant from_one : table_t := (
    (40014, 40692, 999999671),
    (1601120196, 1655838864, 974519621),
    (1346387765, 2103410263, 647483907),
    (439883729, 1872071452, 333085590),
    (732249858, 652912057, 36944544),
    (2127568003, 1780294415, 161711870));
  constant from_1234 : table_t := (
    (49377276, 231049176, 915402414),
    (97443904, 170748970, 965864655),
    (1437707811, 1008291475, 199962569));
begin
  process
    variable seed1, seed2 : positive := 1;
    variable rand : real;
  begin
    for i in from_one'range loop
      uniform(seed1, seed2, rand);
      assert seed1 = from_one(i)(0) and seed2 = from_one(i)(1)
        and integer(rand * 1.0e9) = from_one(i)(2)
        report "uniform row " & integer'image(i) & " differs" severity failure;
    end loop;
    seed1 := 1234;
    seed2 := 5678;
    for i in from_1234'range loop
      uniform(seed1, seed2, rand);
      assert seed1 = from_1234(i)(0) and seed2 = from_1234(i)(1)
        and integer(rand * 1.0e9) = from_1234(i)(2)
        report "uniform seeded row " & integer'image(i) & " differs" severity failure;
    end loop;
    report "all uniform values match";
    wait;
  end process;
end architecture;
