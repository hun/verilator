-- DESCRIPTION: Verilator: VHDL a selected architecture must exist
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity leaf is
  port (y : out integer);
end;

architecture rtl of leaf is
begin
  y <= 1;
end;

entity t is
  port (y : out integer);
end;

architecture rtl of t is
begin
  u : entity work.leaf(nope) port map (y => y);
end;
