-- DESCRIPTION: Verilator: an inout port has no driver a cycle model can resolve
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i   : in std_logic;
    io  : inout std_logic;
    o   : out std_logic
  );
end entity t;

architecture rtl of t is
begin
  o <= i;
end architecture rtl;