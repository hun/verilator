-- DESCRIPTION: Verilator: VHDL unrelated array comparison diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- std_logic_vector, unsigned, signed and bit_vector are distinct array types,
-- and VHDL declares no comparison between them (IEEE 1076-2008 9.2.3). GHDL
-- rejects every comparison below; the frontend must too.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    y0 : out bit;
    y1 : out bit;
    y2 : out bit
  );
end entity;

architecture rtl of t is
  signal slv : std_logic_vector(3 downto 0);
  signal u   : unsigned(3 downto 0);
  signal s   : signed(3 downto 0);
  signal bv  : bit_vector(3 downto 0);
begin
  y0 <= '1' when slv = u else '0';
  y1 <= '1' when u = s else '0';
  y2 <= '1' when bv = slv else '0';
end architecture;
