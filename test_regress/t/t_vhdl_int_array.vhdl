-- DESCRIPTION: Verilator: VHDL arrays of integer elements
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i   : in  integer;
    q   : out integer;
    r   : out integer;
    len : out integer
  );
end;

architecture rtl of t is
  subtype small_t is integer range 0 to 15;
  type amem_t is array (0 to 3) of small_t;
  type dmem_t is array (3 downto 0) of natural range 0 to 63;
  signal amem : amem_t;
  signal dmem : dmem_t;
begin
  amem(0) <= 1;
  amem(1) <= 2;
  amem(2) <= 3;
  amem(3) <= 4;
  dmem(3) <= 10;
  dmem(2) <= 20;
  dmem(1) <= 30;
  dmem(0) <= 40;
  q <= amem(i);
  r <= dmem(i);
  len <= amem'length;
end;
