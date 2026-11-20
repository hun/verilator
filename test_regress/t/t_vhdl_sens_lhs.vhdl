-- DESCRIPTION: Verilator: VHDL process(all) is sensitive to a target index signal
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
    d    : in  std_logic;
    addr : in  integer;
    q2   : out std_logic;
    q3   : out std_logic
  );
end;

architecture rtl of t is
  type mem_t is array (0 to 3) of std_logic;
begin
  -- An indexed name evaluates its index (IEEE 1076-2008 8.4), so process(all)
  -- wakes when addr changes even though addr appears only on the left side.
  -- GHDL: with d = '1', setting addr = 2 writes mem(2).
  proc : process (all)
    variable mem : mem_t := (others => '0');
  begin
    mem(addr) := d;
    q2 <= mem(2);
    q3 <= mem(3);
  end process;
end;
