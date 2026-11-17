-- DESCRIPTION: Verilator: VHDL context declaration imports its packages
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
context work.outer_ctx;

entity t is
  port (o : out unsigned(3 downto 0));
end;

architecture rtl of t is
begin
  -- `unsigned`, `to_unsigned` and WIDTH all come from the context
  o <= to_unsigned(9, WIDTH);
end;
