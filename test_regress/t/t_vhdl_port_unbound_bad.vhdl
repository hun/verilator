-- DESCRIPTION: Verilator: VHDL unbound input ports must have a default
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- IEEE 1076-2008 6.5.7.1: an unassociated input takes its default; without a
-- default it is an error, whether it is omitted or explicitly open.

library ieee;
use ieee.std_logic_1164.all;

entity leaf is
  port (
    a : in  std_logic;
    b : out integer
  );
end;

architecture rtl of leaf is
begin
  b <= 1;
end;

entity t is
  port (o : out integer);
end;

architecture rtl of t is
begin
  -- The named input is omitted
  u1 : entity work.leaf port map (b => o);
  -- The named input is explicitly open
  u2 : entity work.leaf port map (a => open, b => o);
end;
