-- DESCRIPTION: Verilator: VHDL bit-string literals and bit scalars concatenated with a bit_vector
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    f  : in  bit;
    n  : in  bit;
    sk : in  bit_vector(3 downto 0);
    o1 : out bit_vector(15 downto 0);
    o2 : out bit_vector(9 downto 0);
    o3 : out bit_vector(5 downto 0)
  );
end;

architecture rtl of t is
begin
  o1 <= x"00" & f & n & "00" & sk;
  o2 <= f & n & "00" & sk & '1' & '0';
  o3 <= "10" & sk;
end;
