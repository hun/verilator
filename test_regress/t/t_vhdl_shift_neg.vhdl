-- DESCRIPTION: Verilator: VHDL shifts with a negative count are the opposite shift
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

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
