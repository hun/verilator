-- DESCRIPTION: Verilator: an unconstrained vector generic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A generic of type std_logic_vector without a constraint takes its bounds from
-- each instance's actual, and the value is a constant that the design can use: as a
-- port range, as a value, and through a function that converts it to an integer
-- (whose operand must stay a vector when the conversion folds).

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package pk is
  function ones(v : std_logic_vector) return integer;
end package;

package body pk is
  function ones(v : std_logic_vector) return integer is
  begin
    return to_integer(unsigned(v));
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.pk.all;

entity gen_blk is
  generic (POLY : std_logic_vector; SEED : std_logic_vector);
  port (
    v : out std_logic_vector(POLY'length - 1 downto 0);
    n : out integer
  );
end entity;

architecture rtl of gen_blk is
begin
  assert POLY'length = SEED'length report "lengths differ" severity failure;
  v <= POLY;
  n <= ones(SEED);
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o1 : out std_logic_vector(5 downto 0);
    n1 : out integer;
    o2 : out std_logic_vector(3 downto 0);
    n2 : out integer
  );
end entity;

architecture rtl of t is
begin
  u1 : entity work.gen_blk
    generic map (POLY => "110000", SEED => "111111")
    port map (v => o1, n => n1);

  u2 : entity work.gen_blk
    generic map (POLY => "1010", SEED => "0011")
    port map (v => o2, n => n2);
end architecture;
