-- DESCRIPTION: Verilator: VHDL top-level inout port without enable pins
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    oe : in std_logic;
    d  : in std_logic;
    io : inout std_logic;
    y  : out std_logic
  );
end;

architecture rtl of t is
begin
  io <= d when oe = '1' else 'Z';
  y <= io;
end;
