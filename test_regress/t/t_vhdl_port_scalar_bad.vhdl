-- DESCRIPTION: Verilator: VHDL scalar port actuals and defaults must conform
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- IEEE 1076-2008 6.5.2.2: a scalar actual of another base type is an error,
-- not a value to resize. The same holds for a port's default expression.

library ieee;
use ieee.std_logic_1164.all;

entity leaf is
  port (
    s : in  std_logic;
    b : in  boolean;
    n : in  integer;
    o : out integer
  );
end;

architecture rtl of leaf is
begin
  o <= n;
end;

entity leaf_bad_default is
  port (
    a : in  std_logic := 5;
    o : out integer
  );
end;

architecture rtl of leaf_bad_default is
begin
  o <= 1;
end;

entity t is
  port (
    i  : in  integer;
    bo : in  boolean;
    o  : out integer
  );
end;

architecture rtl of t is
begin
  -- An integer actual on a std_logic port, a boolean actual on a std_logic
  -- port, and an integer literal on a std_logic port
  u1 : entity work.leaf port map (s => i, b => bo, n => 5, o => o);
  u2 : entity work.leaf port map (s => 5, b => true, n => 1, o => open);
  -- An integer default on a std_logic port
  u3 : entity work.leaf_bad_default port map (o => open);
end;
