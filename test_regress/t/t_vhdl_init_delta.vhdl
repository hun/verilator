-- DESCRIPTION: Verilator: VHDL a signal read in the time-zero activation keeps its old value
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in  std_logic;
    b_o : out std_logic
  );
end;

architecture rtl of t is
  signal aa : std_logic := '0';
  signal bb : std_logic := '0';
begin
  -- The time-zero activation is a normal one: a signal assignment updates the
  -- signal only at the end of the delta (IEEE 1076-2008 14.7.5.3), so the read of
  -- aa after assigning it still sees the old value. GHDL: b_o is '0' after
  -- the first activation and '1' after the first clock edge.
  process (clk)
  begin
    aa <= '1';
    bb <= aa;
  end process;

  b_o <= bb;
end;
