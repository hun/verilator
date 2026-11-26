-- DESCRIPTION: Verilator: VHDL index argument of 'length past the array dimensions is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- s has one dimension, so s'length(2) names a dimension it does not have; GHDL rejects it too.

entity t is
  port (clk : in bit; o : out bit);
end entity t;

architecture rtl of t is
  signal sb : bit;
  signal s : bit_vector(3 downto 0);
begin
  o <= '1' when s'length(2) = 1 else '0';
end architecture rtl;
