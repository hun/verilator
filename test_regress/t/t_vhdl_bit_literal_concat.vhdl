-- DESCRIPTION: Verilator: VHDL bit-string literal concatenated with a bit_vector
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    d  : in  bit_vector(7 downto 0);
    s  : in  bit;
    o1 : out bit_vector(7 downto 0);
    o2 : out bit_vector(15 downto 0);
    o3 : out bit_vector(7 downto 0);
    o4 : out bit_vector(11 downto 0)
  );
end;

architecture rtl of t is
begin
  o1 <= "000" & d(7 downto 3);
  o2 <= '0' & x"8" & d(5 downto 0) & x"0" & "0";
  o3 <= "00" & d(7 downto 2) when s = '1' else d;
  o4 <= d & "0101";
end;
