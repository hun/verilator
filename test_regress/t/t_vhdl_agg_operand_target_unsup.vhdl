-- DESCRIPTION: Verilator: VHDL aggregate with a non-static choice range as an operand is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The aggregate is an operand of & with no target type, and its choice bounds are not static.

entity t is
  port (clk : in bit; o : out bit);
end entity t;

architecture rtl of t is
  signal sb : bit;
begin
  process (clk)
    variable v : bit_vector(4 downto 0);
    variable n : natural := 3;
  begin
    v := (n downto 0 => '1') & sb;
    o <= v(0);
  end process;
end architecture rtl;
