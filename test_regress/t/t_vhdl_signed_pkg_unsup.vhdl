-- DESCRIPTION: Verilator: VHDL std_logic_signed arithmetic is refused
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_signed.all;

entity t is
  port (
    a : in std_logic_vector(3 downto 0);
    b : in std_logic_vector(3 downto 0);
    o : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- std_logic_signed makes this signed; the frontend does not model that yet
  o <= a + b;
end;
