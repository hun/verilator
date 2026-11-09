-- DESCRIPTION: Verilator: VHDL component bound to a non-VHDL unit
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in std_logic;
    y : out std_logic
  );
end;

architecture rtl of t is
  component other is
    port (
      i : in std_logic;
      o : out std_logic
    );
  end component;
begin
  u : other port map (i => a, o => y);
end;
