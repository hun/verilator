-- DESCRIPTION: Verilator: rising_edge and falling_edge of a bit and of a boolean
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t_vhdl_edge_bit_bool is
  port (clk : in bit; ck : in boolean; d : in bit;
        q : out bit; n : out bit);
end;

architecture r of t_vhdl_edge_bit_bool is
begin
  process (clk)
  begin
    if rising_edge(clk) then
      q <= d;
    end if;
  end process;

  process (ck)
  begin
    if falling_edge(ck) then
      n <= d;
    end if;
  end process;
end;
