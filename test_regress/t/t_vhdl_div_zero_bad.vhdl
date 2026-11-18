-- DESCRIPTION: Verilator: VHDL division by zero is refused
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- Dividing by zero is an error (IEEE 1076-2008 9.1; subset row 4.5)

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
  constant q : integer := 100 / 0;
begin
  y <= q;
end;
