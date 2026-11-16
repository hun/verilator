-- DESCRIPTION: Verilator: a conversion to a real has no synthesizable form
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o : out std_logic
  );
end entity t;

architecture rtl of t is
  function to_real(value : integer) return real is
  begin
    return real(value);
  end function to_real;
begin
  -- Both operands are calls, so the conversion inside each is lowered
  o <= '1' when to_real(3) > to_real(0) else '0';
end architecture rtl;