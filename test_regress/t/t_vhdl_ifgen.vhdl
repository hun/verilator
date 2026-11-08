-- DESCRIPTION: Verilator: VHDL if-generate and elsif-generate selection
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in std_logic;
    b : in std_logic;
    c : in std_logic;
    y : out std_logic;
    z : out std_logic
  );
end;

architecture rtl of t is
begin
  -- True branch
  g1 : if true generate
  begin
    y <= a;
  else generate
  begin
    y <= b;
  end generate;

  -- Elsif chain: both conditions false, so the final else is selected
  g2 : if false generate
  begin
    z <= a;
  elsif false generate
  begin
    z <= b;
  else generate
  begin
    z <= c;
  end generate;
end;
