-- DESCRIPTION: Verilator: VHDL a postponed process and assertion
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t_vhdl_postponed is
  port (x : in  bit;
        y : out bit;
        z : out bit);
end t_vhdl_postponed;

architecture behav of t_vhdl_postponed is
begin
  chk : postponed assert x = x report "x differs from itself";
  postponed assert x = x report "x differs from itself again";

  lbl : postponed process (x)
  begin
    y <= not x;
  end postponed process lbl;

  postponed process (x)
  begin
    z <= x;
  end process;
end behav;
