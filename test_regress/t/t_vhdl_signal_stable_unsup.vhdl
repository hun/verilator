-- DESCRIPTION: Verilator: VHDL 'stable signal attribute in a process is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The 'stable attribute of a signal is legal in an expression; the lowering has no form for it.

entity t is
  port (clk : in bit; o : out bit);
end entity t;

architecture rtl of t is
  signal sb : bit;
begin
  process (clk) begin
    if sb'stable then
      o <= '1';
    else
      o <= '0';
    end if;
  end process;
end architecture rtl;
