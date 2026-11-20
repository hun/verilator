-- DESCRIPTION: Verilator: VHDL configuration specifications are refused
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- The subset does not list configuration specifications, so a silent skip
-- would bind the wrong architecture.

entity a2 is
  port (y : out integer);
end;

architecture rtl of a2 is
begin
  y <= 1;
end;

entity t is
  port (y : out integer);
end;

architecture rtl of t is
  component a2 is
    port (y : out integer);
  end component;
  for u : a2 use entity work.a2(rtl);
begin
  u : a2 port map (y => y);
end;
