-- DESCRIPTION: Verilator: a VHDL testbench uses simulation delays the frontend does not model
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity tb is
end entity tb;

architecture sim of tb is
  signal clk : std_logic := '0';
begin
  -- A clock generator: a delayed signal assignment
  clk <= not clk after 5 ns;
end architecture sim;
