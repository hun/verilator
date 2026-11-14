-- DESCRIPTION: Verilator: a predefined scalar type mark as a conversion
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o_bool  : out std_logic;
    o_logic : out std_logic
  );
end entity t;

architecture rtl of t is
  constant one : std_ulogic := '1';
  -- A predefined scalar type mark applied to a value is a conversion, not a name
  constant flag : boolean    := boolean(3 = 3);
  constant bit1 : std_logic  := std_logic(one);
begin
  o_bool  <= '1' when flag else '0';
  o_logic <= bit1;
end architecture rtl;