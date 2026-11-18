-- DESCRIPTION: Verilator: VHDL association lists may not mix forms
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A port or generic map is either all positional or all named (IEEE 1076-2008
-- 6.5.7.2); GHDL refuses this list.

entity child is
  port (
    p1 : in  bit;
    p2 : in  bit;
    y  : out bit
  );
end;

architecture rtl of child is
begin
  y <= p1 and p2;
end;

entity t is
  port (
    a : in  bit;
    b : in  bit;
    y : out bit
  );
end;

architecture rtl of t is
begin
  u1 : entity work.child port map (a, p2 => b, y => open);
end;
