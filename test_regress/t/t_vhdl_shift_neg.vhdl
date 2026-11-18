-- DESCRIPTION: Verilator: VHDL shifts with a negative count are the opposite shift
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity t is
  port (
    b  : in  bit_vector(3 downto 0);
    n  : in  integer;
    y1 : out bit_vector(3 downto 0);
    y2 : out bit_vector(3 downto 0);
    y3 : out bit_vector(3 downto 0);
    y4 : out bit_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- A negative count is the opposite shift (IEEE 1076-2008 9.2.4): sll(-1)
  -- is srl(1), and so on around the four operators. The expected values are
  -- the ones GHDL reports for b = "1011".
  y1 <= b sll n;
  y2 <= b srl n;
  y3 <= b sla n;
  y4 <= b sra n;
end;
