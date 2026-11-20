-- DESCRIPTION: Verilator: VHDL a selected assignment with a computed choice
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A selected assignment's choice may be an expression of constants, not only a
-- literal: poc's ALU writes `when F7_OP_ADD & F3_OP_ADD =>`. The choice is folded
-- to a value and compared like any other. Every value below is the one GHDL reports.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_choice_expr is
  port (
    sel : in  std_logic_vector(2 downto 0);
    o   : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t_vhdl_choice_expr is
begin
  with sel select
    o <= "0001" when "00" & '0',
         "0010" when "00" & '1',
         "0100" when "01" & '0',
         "1111" when others;
end architecture;