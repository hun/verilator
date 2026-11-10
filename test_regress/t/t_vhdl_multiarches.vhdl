-- DESCRIPTION: Verilator: VHDL multiple architectures use the last analyzed one
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    y : out std_logic
  );
end;

architecture first of t is
begin
  y <= '0';
end;

architecture last of t is
begin
  y <= '1';
end;
