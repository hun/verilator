-- DESCRIPTION: Verilator: a call of the same function nested in another call's actuals
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
  -- neorv32's top selects its boot address with a call nested in another call of the
  -- same function
  function pick(sel : boolean; a, b : std_logic_vector(3 downto 0))
    return std_logic_vector is
  begin
    if sel then
      return a;
    end if;
    return b;
  end function;

  constant chosen : std_logic_vector(3 downto 0)
    := pick(true, "0001", pick(false, "0010", "0100"));
begin
  o <= chosen;
end architecture rtl;
