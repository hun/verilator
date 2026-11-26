-- DESCRIPTION: Verilator: VHDL bit_vector arithmetic shift fill
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    b     : in bit_vector(3 downto 0);
    n     : in integer range 0 to 5;
    sra_o : out bit_vector(3 downto 0);
    sla_o : out bit_vector(3 downto 0);
    sll_o : out bit_vector(3 downto 0);
    srl_o : out bit_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- The vacated positions use the edge element of the array (IEEE 1076-2008 9.2.4)
  sra_o <= b sra n;
  sla_o <= b sla n;
  sll_o <= b sll n;
  srl_o <= b srl n;
end;
